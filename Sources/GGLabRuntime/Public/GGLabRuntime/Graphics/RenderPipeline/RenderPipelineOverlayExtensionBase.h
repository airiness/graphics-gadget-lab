#pragma once

namespace gglab
{
	class RenderGraph;
	struct RenderFrameContext;
	struct RenderServices;

	class RenderPipelineOverlayExtensionBase
	{
	public:
		RenderPipelineOverlayExtensionBase() noexcept = default;
		RenderPipelineOverlayExtensionBase(const RenderPipelineOverlayExtensionBase&) = delete;
		RenderPipelineOverlayExtensionBase& operator=(const RenderPipelineOverlayExtensionBase&) = delete;
		RenderPipelineOverlayExtensionBase(RenderPipelineOverlayExtensionBase&&) = delete;
		RenderPipelineOverlayExtensionBase& operator=(RenderPipelineOverlayExtensionBase&&) = delete;
		virtual ~RenderPipelineOverlayExtensionBase() = default;

		virtual void AddOverlayPasses(RenderGraph& renderGraph,
			const RenderFrameContext& frameContext, const RenderServices& services) noexcept = 0;
	};
}
