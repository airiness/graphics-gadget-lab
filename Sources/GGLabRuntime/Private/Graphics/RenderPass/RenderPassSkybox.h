#pragma once
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderPass/AtmosphereBindingLayout.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneConstantBindings.h"

namespace gglab
{

	class RenderPassSkybox : public RenderPassBase
	{
	public:
		RenderPassSkybox() noexcept :
			RenderPassBase({
				  .m_TypeName = "Background.Skybox",
				  .m_DisplayName = "Background",
				  .m_CategoryName = "Lighting",
				  .m_Description =
					  "Renders the active background where main-view depth is still background.",
				  .m_Category = RenderPassCategory::Lighting,
				  .m_Type = RenderPassType::Graphics,
				})
		{
		}

		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;
		ResolvedRenderPipeline GetOrCreatePSO(const RenderServices& services, bool physicalPreview) noexcept;

		GraphicsPhysicalPipelineKey m_BaseRecipe{};
		AtmosphereBindingLayout m_AtmosphereLayout;
		ResolvedRenderLayout m_SceneLayout;
		ResolvedBindingRef<SceneGPU> m_SceneReference;
		GraphicsPhysicalPipelineKey m_PhysicalPreviewRecipe{};
		GraphicsPipelineSlot m_PipelineSlot{};
		GraphicsPipelineSlot m_PhysicalPreviewPipelineSlot{};
		bool m_IsInitialized = false;
	};
}
