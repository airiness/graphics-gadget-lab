#pragma once
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderPass/RenderPassBase.h"

namespace gglab
{
	class Renderer;

	class RenderPassSkybox : public RenderPassBase
	{
	public:
		RenderPassSkybox() noexcept : RenderPassBase({
			.m_TypeName = "Background.Skybox",
			.m_DisplayName = "Skybox",
			.m_CategoryName = "Lighting",
			.m_Description = "Renders the active HDR environment into the display view before scene geometry.",
			.m_Category = RenderPassCategory::Lighting,
			.m_Type = RenderPassType::Graphics,
		}) {}

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;
		RHIPipelineHandle GetOrCreatePSO(const Renderer& renderer) noexcept;

		GraphicsPhysicalPipelineKey m_BaseRecipe{};
		GraphicsPipelineSlot m_PipelineSlot{};
		bool m_IsInitialized = false;
	};
}
