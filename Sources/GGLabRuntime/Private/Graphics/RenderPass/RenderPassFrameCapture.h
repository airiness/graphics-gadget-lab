#pragma once
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"

namespace gglab
{
	// Copies the display target into a capture-owned readback buffer at one
	// capture tap. The pass is added only for frames with queued requests for its
	// source, so ordinary frames carry no capture work.
	class RenderPassFrameCapture : public RenderPassBase
	{
	public:
		explicit RenderPassFrameCapture(FrameCaptureSource source) noexcept;

		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		static RenderPassInfo MakeInfo(FrameCaptureSource source) noexcept;

		FrameCaptureSource m_Source = FrameCaptureSource::Scene;
	};
}
