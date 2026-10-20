#include "Graphics/Capture/FencedReadback.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"

#include <cstring>
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
		const auto* mapped = static_cast<const std::byte*>(device.MapBuffer(m_Buffer.Get(),
			{ .m_Begin = 0, .m_End = footprint.m_SizeInBytes }));
		if (!mapped)
		{
			return false;
		}
		for (uint32_t row = 0; row < footprint.m_Height; ++row)
		{
			std::memcpy(destination.data() + row * footprint.m_RowSizeInBytes,
				mapped + row * footprint.m_RowPitch, footprint.m_RowSizeInBytes);
		}
		device.UnmapBuffer(m_Buffer.Get(), {});
		return true;
	}

	void FencedReadback::ReleaseBuffer() noexcept
	{
		m_Buffer.Reset();
	}
}
