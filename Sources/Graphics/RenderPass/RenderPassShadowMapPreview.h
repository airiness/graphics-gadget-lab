#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/Pipeline/PipelineCache.h"

namespace gglab
{
	class Renderer;

	class RenderPassShadowMapPreview final : public RenderPassBase
	{
	public:
		RenderPassShadowMapPreview() noexcept : RenderPassBase({
			.m_TypeName = "Debug.ShadowMapPreview",
			.m_DisplayName = "Shadow Map Preview",
			.m_CategoryName = "Debug",
			.m_Description = "Copies and visualizes the directional shadow map for inspection.",
			.m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Graphics,
		}) {}
		~RenderPassShadowMapPreview() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		RHIPipelineHandle GetOrCreatePSO(const Renderer& renderer) noexcept;

	private:
		GraphicsPipelineRecipe m_BaseRecipe{};
		GraphicsPipelineSlot m_PipelineSlot{};
		bool m_IsInitialized = false;
	};
}
