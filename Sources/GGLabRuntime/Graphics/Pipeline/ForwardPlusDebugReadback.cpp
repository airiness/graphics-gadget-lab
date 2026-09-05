#include "Graphics/Pipeline/ForwardPlusDebugReadback.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

namespace gglab
{
	ForwardPlusDebugReadback::~ForwardPlusDebugReadback()
	{
		ReleaseBuffers();
	}

	void ForwardPlusDebugReadback::Initialize(RHIDevice& device, uint32_t bufferCount) noexcept
	{
		if (m_Device == std::addressof(device) && m_Buffers.size() == bufferCount &&
			m_HdrDiffBuffers.size() == bufferCount && m_GridBuffers.size() == bufferCount)
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
		m_GridBuffers.resize(bufferCount);
		m_GridBufferSizes.resize(bufferCount);
		m_PendingSlots.resize(bufferCount);
		m_PendingHdrDiffSlots.resize(bufferCount);
		m_PendingGridSlots.resize(bufferCount);
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

	void ForwardPlusDebugReadback::PrepareGridBuffer(RHIDevice& device, uint32_t bufferIndex,
		const ForwardPlusTileGrid& tileGrid) noexcept
	{
		GGLAB_ASSERT(m_Device == std::addressof(device));
		GGLAB_ASSERT(tileGrid.IsValid());
		GGLAB_ASSERT(bufferIndex < m_GridBuffers.size());
		if (m_Device != std::addressof(device) || !tileGrid.IsValid() ||
			bufferIndex >= m_GridBuffers.size())
		{
			return;
		}

		const uint64_t requiredSize = GetGridReadbackSizeInBytes(tileGrid.m_TileCount);
		if (m_GridBuffers[bufferIndex].IsValid() &&
			m_GridBufferSizes[bufferIndex] == requiredSize)
		{
			return;
		}

		GGLAB_ASSERT(!m_PendingGridSlots[bufferIndex].m_Pending);
		if (m_GridBuffers[bufferIndex].IsValid())
		{
			device.DestroyBuffer(m_GridBuffers[bufferIndex]);
			m_GridBuffers[bufferIndex].Reset();
		}

		const RHIBufferDesc desc{
			.m_SizeInBytes = requiredSize,
			.m_Usage = RHIBufferUsage::CopyDest,
			.m_MemoryUsage = RHIMemoryUsage::GpuToCpu,
			.m_DebugName = "ForwardPlus.GridReadback",
		};
		m_GridBuffers[bufferIndex] = device.CreateBuffer(desc);
		m_GridBufferSizes[bufferIndex] = requiredSize;
		if (!m_GridBuffers[bufferIndex].IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Forward+ failed to allocate grid readback buffer {} ({} bytes).", bufferIndex,
				requiredSize);
			GGLAB_UNREACHABLE("Forward+ grid readback allocation failed.");
		}
	}

	void ForwardPlusDebugReadback::InvalidateResults() noexcept
	{
		uint64_t currentGeneration = m_RequestGeneration.load(std::memory_order_relaxed);
		for (;;)
		{
			GGLAB_ASSERT_MSG(currentGeneration != std::numeric_limits<uint64_t>::max(),
				"Forward+ readback request generation must never wrap.");
			if (currentGeneration == std::numeric_limits<uint64_t>::max())
			{
				return;
			}
			if (m_RequestGeneration.compare_exchange_weak(currentGeneration,
				currentGeneration + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
			{
				break;
			}
		}

		std::scoped_lock lock(m_ResultMutex);
		m_Latest = {};
		m_LatestHdrDiff = {};
		m_LatestGrid.reset();
	}

	void ForwardPlusDebugReadback::ResetPerformance() noexcept
	{
		std::scoped_lock lock(m_ResultMutex);
		m_Performance = {};
	}

	void ForwardPlusDebugReadback::SetSelectedTile(uint32_t tileX, uint32_t tileY) noexcept
	{
		const uint32_t previousX = m_SelectedTileX.exchange(tileX, std::memory_order_relaxed);
		const uint32_t previousY = m_SelectedTileY.exchange(tileY, std::memory_order_relaxed);
		if (previousX != tileX || previousY != tileY)
		{
			InvalidateResults();
		}
	}

	void ForwardPlusDebugReadback::ConsumeCompletedSlot(uint32_t bufferIndex) noexcept
	{
		if (!m_Device || bufferIndex >= m_Buffers.size() ||
			bufferIndex >= m_HdrDiffBuffers.size() || bufferIndex >= m_GridBuffers.size() ||
			bufferIndex >= m_PendingSlots.size() ||
			bufferIndex >= m_PendingHdrDiffSlots.size() ||
			bufferIndex >= m_PendingGridSlots.size())
		{
			return;
		}

		const uint64_t currentGeneration = GetCurrentGeneration();
		PendingSlot& pending = m_PendingSlots[bufferIndex];
		if (pending.m_Pending && !IsForwardPlusReadbackGenerationCurrent(
			pending.m_RequestGeneration, currentGeneration))
		{
			pending.m_Pending = false;
		}
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
				pending.m_Pending = false;
			}
			else
			{
				ForwardPlusTileReadback result{
					.m_FrameSerial = pending.m_FrameSerial,
					.m_RequestGeneration = pending.m_RequestGeneration,
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

				{
					std::scoped_lock lock(m_ResultMutex);
					if (ShouldPublishForwardPlusReadback(result.m_RequestGeneration,
						result.m_FrameSerial, GetCurrentGeneration(),
						m_Latest.m_RequestGeneration, m_Latest.m_FrameSerial))
					{
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
			}
		}

		PendingHdrDiffSlot& pendingHdrDiff = m_PendingHdrDiffSlots[bufferIndex];
		if (pendingHdrDiff.m_Pending && !IsForwardPlusReadbackGenerationCurrent(
			pendingHdrDiff.m_RequestGeneration, currentGeneration))
		{
			pendingHdrDiff.m_Pending = false;
		}
		if (pendingHdrDiff.m_Pending)
		{
			const RHIBufferHandle hdrDiffBuffer = m_HdrDiffBuffers[bufferIndex];
			const void* mappedHdrDiff = m_Device->MapBuffer(hdrDiffBuffer, {
				.m_Begin = 0,
				.m_End = HdrDiffReadbackSizeInBytes,
				});
			if (!mappedHdrDiff)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"Forward+ failed to map completed HDR diff slot {}.", bufferIndex);
				pendingHdrDiff.m_Pending = false;
			}
			else
			{
				std::array<uint32_t, 4> packed{};
				std::memcpy(packed.data(), mappedHdrDiff, sizeof(packed));
				m_Device->UnmapBuffer(hdrDiffBuffer, {});
				pendingHdrDiff.m_Pending = false;

				ForwardPlusHdrDiffReadback result{
					.m_FrameSerial = pendingHdrDiff.m_FrameSerial,
					.m_RequestGeneration = pendingHdrDiff.m_RequestGeneration,
					.m_Width = pendingHdrDiff.m_Width,
					.m_Height = pendingHdrDiff.m_Height,
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
					if (ShouldPublishForwardPlusReadback(result.m_RequestGeneration,
						result.m_FrameSerial, GetCurrentGeneration(),
						m_LatestHdrDiff.m_RequestGeneration,
						m_LatestHdrDiff.m_FrameSerial))
					{
						m_LatestHdrDiff = result;
						if (!m_LoggedFirstHdrDiff)
						{
							GGLAB_LOG_GRAPHICS_INFO(
								"Forward+ HDR diff completed (frame={}, maxAbs={}, maxRelLum={}, pixel=({}, {}), compared={}).",
								result.m_FrameSerial, result.m_MaxAbsoluteError,
								result.m_MaxRelativeLuminanceError, result.m_MaxErrorPixelX,
								result.m_MaxErrorPixelY, result.m_ComparedPixelCount);
							m_LoggedFirstHdrDiff = true;
						}
					}
				}
			}
		}

		PendingGridSlot& pendingGrid = m_PendingGridSlots[bufferIndex];
		if (pendingGrid.m_Pending && !IsForwardPlusReadbackGenerationCurrent(
			pendingGrid.m_RequestGeneration, currentGeneration))
		{
			pendingGrid.m_Pending = false;
		}
		if (!pendingGrid.m_Pending)
		{
			return;
		}

		const uint64_t readbackSize =
			GetGridReadbackSizeInBytes(pendingGrid.m_TileGrid.m_TileCount);
		const RHIBufferHandle gridBuffer = m_GridBuffers[bufferIndex];
		const void* mappedGrid = m_Device->MapBuffer(gridBuffer, {
			.m_Begin = 0,
			.m_End = readbackSize,
			});
		if (!mappedGrid)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Forward+ failed to map completed grid readback slot {}.", bufferIndex);
			pendingGrid.m_Pending = false;
			return;
		}

		auto result = std::make_shared<ForwardPlusGridReadback>();
		result->m_FrameSerial = pendingGrid.m_FrameSerial;
		result->m_RequestGeneration = pendingGrid.m_RequestGeneration;
		result->m_TileGrid = pendingGrid.m_TileGrid;
		result->m_Headers.resize(pendingGrid.m_TileGrid.m_TileCount);
		result->m_DepthRanges.resize(pendingGrid.m_TileGrid.m_TileCount);
		std::memcpy(result->m_Headers.data(), mappedGrid,
			GetGridHeadersSizeInBytes(pendingGrid.m_TileGrid.m_TileCount));
		std::memcpy(result->m_DepthRanges.data(),
			static_cast<const std::byte*>(mappedGrid) +
			GetGridDepthRangesOffset(pendingGrid.m_TileGrid.m_TileCount),
			static_cast<size_t>(pendingGrid.m_TileGrid.m_TileCount) *
			sizeof(ForwardPlusTileDepthRange));
		m_Device->UnmapBuffer(gridBuffer, {});
		pendingGrid.m_Pending = false;
		result->m_IsValid = true;
		std::scoped_lock lock(m_ResultMutex);
		const uint64_t publishedGeneration =
			m_LatestGrid ? m_LatestGrid->m_RequestGeneration : 0;
		const uint64_t publishedFrameSerial =
			m_LatestGrid ? m_LatestGrid->m_FrameSerial : 0;
		if (ShouldPublishForwardPlusReadback(result->m_RequestGeneration,
			result->m_FrameSerial, GetCurrentGeneration(), publishedGeneration,
			publishedFrameSerial))
		{
			m_LatestGrid = std::move(result);
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
			.m_RequestGeneration = GetCurrentGeneration(),
			.m_TileGrid = tileGrid,
			.m_TileX = tileX,
			.m_TileY = tileY,
			.m_Pending = true,
		};
		m_ScheduledCount.fetch_add(1, std::memory_order_relaxed);
	}

	void ForwardPlusDebugReadback::MarkHdrDiffScheduled(uint32_t bufferIndex,
		uint64_t frameSerial, uint32_t width, uint32_t height) noexcept
	{
		GGLAB_ASSERT(bufferIndex < m_PendingHdrDiffSlots.size());
		if (bufferIndex >= m_PendingHdrDiffSlots.size())
		{
			return;
		}
		m_PendingHdrDiffSlots[bufferIndex] = {
			.m_FrameSerial = frameSerial,
			.m_RequestGeneration = GetCurrentGeneration(),
			.m_Width = width,
			.m_Height = height,
			.m_Pending = true,
		};
	}

	void ForwardPlusDebugReadback::MarkGridScheduled(uint32_t bufferIndex, uint64_t frameSerial,
		const ForwardPlusTileGrid& tileGrid) noexcept
	{
		GGLAB_ASSERT(bufferIndex < m_PendingGridSlots.size());
		if (bufferIndex >= m_PendingGridSlots.size())
		{
			return;
		}
		m_PendingGridSlots[bufferIndex] = {
			.m_FrameSerial = frameSerial,
			.m_RequestGeneration = GetCurrentGeneration(),
			.m_TileGrid = tileGrid,
			.m_Pending = true,
		};
	}

	void ForwardPlusDebugReadback::RecordLegacyGpuTiming(
		uint64_t frameSerial, double opaqueMilliseconds) noexcept
	{
		std::scoped_lock lock(m_ResultMutex);
		m_Performance.m_LegacyFrameSerial = frameSerial;
		m_Performance.m_LegacyOpaqueMilliseconds = opaqueMilliseconds;
		m_Performance.m_HasLegacySample = true;
	}

	void ForwardPlusDebugReadback::RecordForwardPlusGpuTiming(uint64_t frameSerial,
		double cullMilliseconds, double opaqueMilliseconds) noexcept
	{
		std::scoped_lock lock(m_ResultMutex);
		m_Performance.m_ForwardPlusFrameSerial = frameSerial;
		m_Performance.m_ForwardPlusCullMilliseconds = cullMilliseconds;
		m_Performance.m_ForwardPlusOpaqueMilliseconds = opaqueMilliseconds;
		m_Performance.m_HasForwardPlusSample = true;
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

	RHIBufferHandle ForwardPlusDebugReadback::GetGridBuffer(uint32_t bufferIndex) const noexcept
	{
		return bufferIndex < m_GridBuffers.size() ? m_GridBuffers[bufferIndex]
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

	std::shared_ptr<const ForwardPlusGridReadback>
		ForwardPlusDebugReadback::GetLatestGrid() const noexcept
	{
		std::scoped_lock lock(m_ResultMutex);
		return m_LatestGrid;
	}

	ForwardPlusPerformanceReadback ForwardPlusDebugReadback::GetPerformance() const noexcept
	{
		std::scoped_lock lock(m_ResultMutex);
		return m_Performance;
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
			for (const RHIBufferHandle buffer : m_GridBuffers)
			{
				if (buffer.IsValid())
				{
					m_Device->DestroyBuffer(buffer);
				}
			}
		}
		m_Buffers.clear();
		m_HdrDiffBuffers.clear();
		m_GridBuffers.clear();
		m_GridBufferSizes.clear();
		m_PendingSlots.clear();
		m_PendingHdrDiffSlots.clear();
		m_PendingGridSlots.clear();
		m_Device = nullptr;
		m_LoggedFirstResult = false;
		m_LoggedFirstHdrDiff = false;
		std::scoped_lock lock(m_ResultMutex);
		m_Latest = {};
		m_LatestHdrDiff = {};
		m_LatestGrid.reset();
		m_Performance = {};
	}
}
