#pragma once
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace gglab
{
	class RenderGraph;
	struct RenderFrameContext;
	struct RenderServices;

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
		// nothing is queued; requests that the display target or payload budget cannot satisfy
		// finish as Failed instead of being bound. A Diagnostic tap binds only the
		// requests for the pending diagnostic tap. Frame-restricted requests bind
		// only to their named frame.
		[[nodiscard]] virtual std::optional<FrameCaptureTapTarget> BindTap(uint64_t frameSerial,
			FrameCaptureSource source, const RHITextureDesc& displayTargetDesc) noexcept = 0;
		// Tap of the oldest queued Diagnostic request; a frame records one tap.
		[[nodiscard]] virtual std::optional<PostProcessDebugTap> GetPendingDiagnosticTap()
			const noexcept = 0;
		// Fails the queued requests for the pending diagnostic tap when the frame has
		// no source for it.
		virtual void FailPendingDiagnosticRequests(std::string_view failure) noexcept = 0;
		// Debug verification request carried by this frame's production counters. Only
		// fixtures that must add the producer of an injection point consult it; injection
		// points themselves read the request from the counter buffer.
		[[nodiscard]] virtual std::optional<NonFiniteInjection> GetNonFiniteInjection() const noexcept
		{
			return std::nullopt;
		}
	};

	// Adds the capture tap pass for the frame's queued requests of one source; frames
	// without such requests receive no pass. Scene and Composited taps copy the display
	// view's back buffer, so a pipeline adds them after the last writer of that tap's
	// content and before the present export.
	void AddFrameCaptureTapPass(RenderGraph& rg, const RenderFrameContext& context,
		const RenderServices& services, FrameCaptureSource source) noexcept;
}
