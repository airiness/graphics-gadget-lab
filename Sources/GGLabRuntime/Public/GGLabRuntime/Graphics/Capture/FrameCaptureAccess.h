#pragma once
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"

#include <cstdint>
#include <optional>

namespace gglab
{
	// Readback destination of one capture tap in one frame. The capture service
	// owns the buffer until the frame's GPU work completes.
	struct FrameCaptureTapTarget
	{
		RHIBufferHandle m_Buffer{};
		RHIBufferDesc m_BufferDesc{};
		RHITextureCopyFootprint m_Footprint{};
	};

	// Pass-facing capture seam used by render pipelines while building a frame.
	class RenderFrameCaptureAccess
	{
	public:
		virtual ~RenderFrameCaptureAccess() = default;

		[[nodiscard]] virtual bool HasPendingRequests(FrameCaptureSource source) const noexcept = 0;
		// Binds every queued request for the source to the frame and returns the
		// readback target that the frame's tap pass must fill. Returns nullopt when
		// nothing is queued; requests that the display target cannot satisfy
		// finish as Failed instead of being bound.
		[[nodiscard]] virtual std::optional<FrameCaptureTapTarget> BindTap(uint64_t frameSerial,
			FrameCaptureSource source, const RHITextureDesc& displayTargetDesc) noexcept = 0;
	};
}
