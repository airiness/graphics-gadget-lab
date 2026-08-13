#pragma once
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/PostProcess/PostProcessColor.h"
#include "Graphics/PostProcess/PostProcessDebug.h"

#include <optional>
#include "Graphics/RenderPass/RenderPassBase.h"

namespace gglab
{
	class Renderer;

	class RenderPassPostProcessPreview final : public RenderPassBase
	{
	public:
		RenderPassPostProcessPreview() noexcept :
			RenderPassBase({
				  .m_TypeName = "PostProcess.Preview",
				  .m_DisplayName = "Post Process Preview",
				  .m_CategoryName = "PostProcess",
				  .m_Description =
					  "Publishes the selected HDR post-process tap to one persistent SDR preview texture.",
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

	private:
		void AddResolvedPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services, RGTextureId source, float sourcePreExposure,
			std::optional<RHITextureViewDesc> sourceViewDesc,
			PostProcessDebugSelection selection) noexcept;
		void EnsureInitialized(const RenderServices& services) noexcept;
		RHIPipelineHandle GetOrCreatePSO(const Renderer& renderer) noexcept;

		GraphicsPhysicalPipelineKey m_BaseRecipe{};
		GraphicsPipelineSlot m_PipelineSlot{};
		bool m_IsInitialized = false;
	};
}
