#include "Graphics/Capture/FencedReadback.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"

#include <cstring>
#include <algorithm>
#include <utility>

namespace gglab
{
	void FencedReadback::SetBuffer(RHIBufferOwner buffer) noexcept
	{
		GGLAB_ASSERT(!IsSubmitted() && !m_Buffer);
		m_Buffer = std::move(buffer);
	}

	void FencedReadback::Submit(const RHIFencePoint& fence) noexcept
	{
		GGLAB_ASSERT(fence.IsValid() && !IsSubmitted());
		m_Fence = fence;
		if (!IsCompletionUnknown())
		{
			m_State = State::Pending;
		}
	}

	RHIBufferHandle FencedReadback::AddBuffer(RHIBufferOwner buffer) noexcept
	{
		GGLAB_ASSERT(!IsSubmitted());
		const auto handle = buffer.Get();
		if (buffer)
		{
			m_AdditionalBuffers.push_back(std::move(buffer));
		}
		return handle;
	}

	void FencedReadback::MarkCompletionUnknown() noexcept
	{
		m_State = State::Unknown;
	}

	RHIFenceQueryResult FencedReadback::Poll(RHIDevice& device) noexcept
	{
		switch (m_State)
		{
		case State::Unsubmitted:
			return RHIFenceQueryResult::Pending;
		case State::Completed:
			return RHIFenceQueryResult::Completed;
		case State::DeviceLost:
			return RHIFenceQueryResult::DeviceLost;
		case State::Unknown:
			return RHIFenceQueryResult::Failed;
		case State::Pending:
			break;
		}
		const RHIFenceQueryResult result = device.QueryFencePoint(m_Fence);
		switch (result)
		{
		case RHIFenceQueryResult::Pending:
			break;
		case RHIFenceQueryResult::Completed:
			m_State = State::Completed;
			break;
		case RHIFenceQueryResult::DeviceLost:
			m_State = State::DeviceLost;
			break;
		case RHIFenceQueryResult::Failed:
			m_State = State::Unknown;
			break;
		}
		return result;
	}

	bool FencedReadback::CopyRows(RHIDevice& device, const RHITextureCopyFootprint& footprint,
		std::span<std::byte> destination) const noexcept
	{
		if (m_State != State::Completed || !m_Buffer || !footprint.IsValid() ||
			destination.size() != footprint.m_RowSizeInBytes * footprint.m_Height)
		{
			return false;
		}
		return Read(device, m_Buffer.Get(), { 0, footprint.m_SizeInBytes },
			[&](std::span<const std::byte> mapped)
			{
				for (uint32_t row = 0; row < footprint.m_Height; ++row)
				{
					std::memcpy(destination.data() + row * footprint.m_RowSizeInBytes,
						mapped.data() + row * footprint.m_RowPitch, footprint.m_RowSizeInBytes);
				}
			});
	}

	bool FencedReadback::Read(RHIDevice& device, RHIBufferHandle buffer,
		RHIMappedBufferRange range,
		const std::function<void(std::span<const std::byte>)>& consume) const noexcept
	{
		const bool owned = buffer.IsValid() && (buffer == m_Buffer.Get() ||
			std::ranges::any_of(m_AdditionalBuffers,
				[buffer](const auto& owner) { return owner.Get() == buffer; }));
		if (m_State != State::Completed || !owned || range.m_End < range.m_Begin)
		{
			return false;
		}
		const auto* mapped = static_cast<const std::byte*>(device.MapBuffer(buffer, range));
		if (!mapped)
		{
			return false;
		}
		consume({ mapped + range.m_Begin, static_cast<size_t>(range.m_End - range.m_Begin) });
		device.UnmapBuffer(buffer, {});
		return true;
	}

	void FencedReadback::ReleaseBuffers() noexcept
	{
		m_Buffer.Reset();
		m_AdditionalBuffers.clear();
	}
}
