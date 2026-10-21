#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"

#include <cstddef>
#include <functional>
#include <span>
#include <vector>

namespace gglab
{
	class RHIDevice;

	// A completion gate with optional owned readback storage. Destruction requires
	// completion, discarded recording, verified idle or device loss. Unknown work
	// must remain owned (or be abandoned) without mapping its contents.
	class FencedReadback
	{
	public:
		void SetBuffer(RHIBufferOwner buffer) noexcept;
		[[nodiscard]] RHIBufferHandle AddBuffer(RHIBufferOwner buffer) noexcept;
		void Submit(const RHIFencePoint& fence) noexcept;
		void MarkCompletionUnknown() noexcept;
		[[nodiscard]] RHIFenceQueryResult Poll(RHIDevice& device) noexcept;
		[[nodiscard]] bool IsSubmitted() const noexcept { return m_Fence.IsValid(); }
		[[nodiscard]] bool IsCompletionUnknown() const noexcept { return m_State == State::Unknown; }
		[[nodiscard]] bool IsCompleted() const noexcept { return m_State == State::Completed; }
		[[nodiscard]] const RHIFencePoint& GetFence() const noexcept { return m_Fence; }
		[[nodiscard]] RHIBufferHandle GetBuffer() const noexcept { return m_Buffer.Get(); }
		// Maps only after Poll certifies completion; no mapping escapes this call.
		[[nodiscard]] bool CopyRows(RHIDevice& device, const RHITextureCopyFootprint& footprint,
			std::span<std::byte> destination) const noexcept;
		// All storage in a tap shares one fence verdict. The consumer runs synchronously
		// within the mapping and must not retain the borrowed bytes.
		[[nodiscard]] bool Read(RHIDevice& device, RHIBufferHandle buffer,
			RHIMappedBufferRange range,
			const std::function<void(std::span<const std::byte>)>& consume) const noexcept;
		// The caller certifies safe retirement, including teardown after verified idle.
		void ReleaseBuffers() noexcept;

	private:
		enum class State { Unsubmitted, Pending, Completed, DeviceLost, Unknown };
		RHIBufferOwner m_Buffer;
		std::vector<RHIBufferOwner> m_AdditionalBuffers;
		RHIFencePoint m_Fence{};
		State m_State = State::Unsubmitted;
	};
}
