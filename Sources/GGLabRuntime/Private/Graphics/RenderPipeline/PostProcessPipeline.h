#pragma once
#include "Graphics/RenderPass/RenderPassBloom.h"
#include "Graphics/RenderPass/RenderPassFinalColor.h"
#include "Graphics/RenderPass/RenderPassPostProcessPreview.h"

namespace gglab
{
	class PostProcessPipeline
	{
	public:
		void AddPasses(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept;
		// Records the display-resolution target of a pending Diagnostic capture, or
		// fails its requests when the frame has no source for the tap.
		void AddDiagnosticCapturePass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept;

	private:
		RenderPassBloom m_BloomPass;
		RenderPassPostProcessPreview m_PreviewPass;
		RenderPassFinalColor m_FinalColorPass;
	};
}
