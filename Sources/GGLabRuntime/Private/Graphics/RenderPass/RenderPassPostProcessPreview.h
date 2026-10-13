#pragma once
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "Graphics/PostProcess/PostProcessColor.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"

#include <optional>
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"

namespace gglab
{

	class RenderPassPostProcessPreview final : public RenderPassBase
	{
	public:
		RenderPassPostProcessPreview() noexcept :
			RenderPassBase({
				  .m_TypeName = "PostProcess.Preview",
				  .m_DisplayName = "Post Process Preview",
				  .m_CategoryName = "PostProcess",
				  .m_Description =
					  "Publishes requested diagnostic taps to inspector-owned SDR preview textures.",
				  .m_Category = RenderPassCategory::PostProcess,
				  .m_Type = RenderPassType::Graphics,
				})
		{
		}
		~RenderPassPostProcessPreview() override = default;

		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;
		void AddPassForTap(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services, const RGPostProcessColor& source,
			PostProcessDebugTap tap, uint32_t bloomPyramidLevel = 0) noexcept;
		// Renders the tap's preview visualization at display resolution into a
		// transient RGBA8 target published as RGDiagnosticCaptureResources, for a
		// Diagnostic capture. Returns false when the frame has no source for the tap.
		[[nodiscard]] bool AddDiagnosticCapturePass(RenderGraph& rg,
			const RenderFrameContext& context, const RenderServices& services,
			PostProcessDebugTap tap) noexcept;

	private:
		void AddPassForChannel(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services, PostProcessPreviewChannel channel) noexcept;
		void AddResolvedPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services, RGTextureId source, float sourcePreExposure,
			std::optional<RHITextureViewDesc> sourceViewDesc,
			PostProcessDebugSelection selection, PostProcessPreviewChannel channel) noexcept;
		void EnsureInitialized(const RenderServices& services) noexcept;
		RHIPipelineHandle GetOrCreatePSO(const RenderServices& services) noexcept;

		GraphicsPhysicalPipelineKey m_BaseRecipe{};
		GraphicsPipelineSlot m_PipelineSlot{};
		bool m_IsInitialized = false;
	};
}
