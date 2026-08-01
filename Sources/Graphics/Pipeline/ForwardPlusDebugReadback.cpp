#include "Core/Precompiled.h"
#include "Graphics/Pipeline/ForwardPlusDebugReadback.h"

#include "Graphics/RHI/RHIDevice.h"

namespace gglab
{
	ForwardPlusDebugReadback::~ForwardPlusDebugReadback()
	{
		ReleaseBuffers();
	}

	void ForwardPlusDebugReadback::Initialize(RHIDevice& device, uint32_t bufferCount) noexcept
	{
		if (m_Device == std::addressof(device) && m_Buffers.size() == bufferCount &&
			m_HdrDiffBuffers.size() == bufferCount)
		{
			return;
		}

		ReleaseBuffers();
		if (bufferCount == 0)
		{
			return;
		}

		m_Device = std::addressof(device);
		m_Buffers.resize(bufferCount);
		m_HdrDiffBuffers.resize(bufferCount);
		m_PendingSlots.resize(bufferCount);
		m_PendingHdrDiffSlots.resize(bufferCount);
		for (uint32_t bufferIndex = 0; bufferIndex < bufferCount; ++bufferIndex)
		{
			RHIBufferDesc desc{};
			desc.m_SizeInBytes = ReadbackSizeInBytes;
			desc.m_Usage = RHIBufferUsage::CopyDest;
			desc.m_MemoryUsage = RHIMemoryUsage::GpuToCpu;
			desc.m_DebugName = "ForwardPlus.DebugReadback";
			m_Buffers[bufferIndex] = device.CreateBuffer(desc);
			if (!m_Buffers[bufferIndex].IsValid())
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"Forward+ failed to allocate debug readback buffer {}.", bufferIndex);
				GGLAB_UNREACHABLE("Forward+ debug readback allocation failed.");
			}

			desc.m_SizeInBytes = HdrDiffReadbackSizeInBytes;
			desc.m_DebugName = "ForwardPlus.HdrDiffReadback";
			m_HdrDiffBuffers[bufferIndex] = device.CreateBuffer(desc);
			if (!m_HdrDiffBuffers[bufferIndex].IsValid())
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"Forward+ failed to allocate HDR diff readback buffer {}.", bufferIndex);
				GGLAB_UNREACHABLE("Forward+ HDR diff readback allocation failed.");
			}
		}
	}

	void ForwardPlusDebugReadback::SetSelectedTile(uint32_t tileX, uint32_t tileY) noexcept
	{
		m_SelectedTileX.store(tileX, std::memory_order_relaxed);
		m_SelectedTileY.store(tileY, std::memory_order_relaxed);
	}

	void ForwardPlusDebugReadback::ConsumeCompletedSlot(uint32_t bufferIndex) noexcept
	{
		if (!m_Device || bufferIndex >= m_Buffers.size() ||
			bufferIndex >= m_HdrDiffBuffers.size() || bufferIndex >= m_PendingSlots.size() ||
			bufferIndex >= m_PendingHdrDiffSlots.size())
		{
			return;
		}

		PendingSlot& pending = m_PendingSlots[bufferIndex];
		if (pending.m_Pending)
		{
			const RHIBufferHandle buffer = m_Buffers[bufferIndex];
			const void* mapped = m_Device->MapBuffer(buffer, {
				.m_Begin = 0,
				.m_End = ReadbackSizeInBytes,
				});
			if (!mapped)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"Forward+ failed to map completed debug readback slot {}.", bufferIndex);
			}
			else
			{
				ForwardPlusTileReadback result{
					.m_FrameSerial = pending.m_FrameSerial,
					.m_TileGrid = pending.m_TileGrid,
					.m_TileX = pending.m_TileX,
					.m_TileY = pending.m_TileY,
					.m_IsValid = true,
				};
				std::memcpy(std::addressof(result.m_Header),
					static_cast<const std::byte*>(mapped) + HeaderReadbackOffset,
					sizeof(result.m_Header));
				std::memcpy(result.m_LightIndices.data(),
					static_cast<const std::byte*>(mapped) + IndicesReadbackOffset,
					sizeof(result.m_LightIndices));
				m_Device->UnmapBuffer(buffer, {});
				pending.m_Pending = false;

				std::scoped_lock lock(m_ResultMutex);
				m_Latest = result;
				if (!m_LoggedFirstResult)
				{
					GGLAB_LOG_GRAPHICS_INFO(
						"Forward+ GPU readback completed (frame={}, tile=({}, {}), offset={}, count={}).",
						result.m_FrameSerial, result.m_TileX, result.m_TileY,
						result.m_Header.m_Offset, result.m_Header.GetCount());
					m_LoggedFirstResult = true;
				}
			}
		}

		PendingHdrDiffSlot& pendingHdrDiff = m_PendingHdrDiffSlots[bufferIndex];
		if (!pendingHdrDiff.m_Pending)
		{
			return;
		}

		const RHIBufferHandle hdrDiffBuffer = m_HdrDiffBuffers[bufferIndex];
		const void* mappedHdrDiff = m_Device->MapBuffer(hdrDiffBuffer, {
			.m_Begin = 0,
			.m_End = HdrDiffReadbackSizeInBytes,
			});
		if (!mappedHdrDiff)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Forward+ failed to map completed HDR diff slot {}.", bufferIndex);
			return;
		}

		std::array<uint32_t, 4> packed{};
		std::memcpy(packed.data(), mappedHdrDiff, sizeof(packed));
		m_Device->UnmapBuffer(hdrDiffBuffer, {});
		pendingHdrDiff.m_Pending = false;

		ForwardPlusHdrDiffReadback hdrDiffResult{
			.m_FrameSerial = pendingHdrDiff.m_FrameSerial,
			.m_MaxAbsoluteError = std::bit_cast<float>(packed[0]),
			.m_MaxRelativeLuminanceError = std::bit_cast<float>(packed[1]),
			.m_MaxErrorPixelX = pendingHdrDiff.m_Width > 0
				? packed[2] % pendingHdrDiff.m_Width
				: 0,
			.m_MaxErrorPixelY = pendingHdrDiff.m_Width > 0
				? packed[2] / pendingHdrDiff.m_Width
				: 0,
			.m_ComparedPixelCount = packed[3],
			.m_IsValid = pendingHdrDiff.m_Width > 0 && pendingHdrDiff.m_Height > 0 &&
				static_cast<uint64_t>(packed[2]) <
					static_cast<uint64_t>(pendingHdrDiff.m_Width) * pendingHdrDiff.m_Height,
		};
		{
			std::scoped_lock lock(m_ResultMutex);
			m_LatestHdrDiff = hdrDiffResult;
			if (!m_LoggedFirstHdrDiff)
			{
				GGLAB_LOG_GRAPHICS_INFO(
					"Forward+ HDR diff completed (frame={}, maxAbs={}, maxRelLum={}, pixel=({}, {}), compared={}).",
					hdrDiffResult.m_FrameSerial, hdrDiffResult.m_MaxAbsoluteError,
					hdrDiffResult.m_MaxRelativeLuminanceError, hdrDiffResult.m_MaxErrorPixelX,
					hdrDiffResult.m_MaxErrorPixelY, hdrDiffResult.m_ComparedPixelCount);
				m_LoggedFirstHdrDiff = true;
			}
		}
	}

	void ForwardPlusDebugReadback::MarkScheduled(uint32_t bufferIndex, uint64_t frameSerial,
		const ForwardPlusTileGrid& tileGrid, uint32_t tileX, uint32_t tileY) noexcept
	{
		GGLAB_ASSERT(bufferIndex < m_PendingSlots.size());
		if (bufferIndex >= m_PendingSlots.size())
		{
			return;
		}
		m_PendingSlots[bufferIndex] = {
			.m_FrameSerial = frameSerial,
			.m_TileGrid = tileGrid,
			.m_TileX = tileX,
			.m_TileY = tileY,
			.m_Pending = true,
		};
		m_ScheduledCount.fetch_add(1, std::memory_order_relaxed);
	}

	void ForwardPlusDebugReadback::MarkHdrDiffScheduled(uint32_t bufferIndex, uint64_t frameSerial,
		uint32_t width, uint32_t height) noexcept
	{
		GGLAB_ASSERT(bufferIndex < m_PendingHdrDiffSlots.size());
		if (bufferIndex >= m_PendingHdrDiffSlots.size())
		{
			return;
		}
		m_PendingHdrDiffSlots[bufferIndex] = {
			.m_FrameSerial = frameSerial,
			.m_Width = width,
			.m_Height = height,
			.m_Pending = true,
		};
	}

	RHIBufferHandle ForwardPlusDebugReadback::GetBuffer(uint32_t bufferIndex) const noexcept
	{
		return bufferIndex < m_Buffers.size() ? m_Buffers[bufferIndex] : RHIBufferHandle{};
	}

	RHIBufferHandle ForwardPlusDebugReadback::GetHdrDiffBuffer(uint32_t bufferIndex) const noexcept
	{
		return bufferIndex < m_HdrDiffBuffers.size() ? m_HdrDiffBuffers[bufferIndex]
			: RHIBufferHandle{};
	}

	uint32_t ForwardPlusDebugReadback::GetSelectedTileX() const noexcept
	{
		return m_SelectedTileX.load(std::memory_order_relaxed);
	}

	uint32_t ForwardPlusDebugReadback::GetSelectedTileY() const noexcept
	{
		return m_SelectedTileY.load(std::memory_order_relaxed);
	}

	ForwardPlusTileReadback ForwardPlusDebugReadback::GetLatest() const noexcept
	{
		std::scoped_lock lock(m_ResultMutex);
		return m_Latest;
	}

	ForwardPlusHdrDiffReadback ForwardPlusDebugReadback::GetLatestHdrDiff() const noexcept
	{
		std::scoped_lock lock(m_ResultMutex);
		return m_LatestHdrDiff;
	}

	void ForwardPlusDebugReadback::ReleaseBuffers() noexcept
	{
		if (m_Device)
		{
			for (const RHIBufferHandle buffer : m_Buffers)
			{
				if (buffer.IsValid())
				{
					m_Device->DestroyBuffer(buffer);
				}
			}
			for (const RHIBufferHandle buffer : m_HdrDiffBuffers)
			{
				if (buffer.IsValid())
				{
					m_Device->DestroyBuffer(buffer);
				}
			}
		}
		m_Buffers.clear();
		m_HdrDiffBuffers.clear();
		m_PendingSlots.clear();
		m_PendingHdrDiffSlots.clear();
		m_Device = nullptr;
		m_LoggedFirstResult = false;
		m_LoggedFirstHdrDiff = false;
	}
}
