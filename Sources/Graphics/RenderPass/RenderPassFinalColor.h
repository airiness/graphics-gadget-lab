#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/Pipeline/PipelineCache.h"

namespace gglab
{
	class Renderer;

	class RenderPassFinalColor final : public RenderPassBase
	{
	public:
		RenderPassFinalColor() noexcept : RenderPassBase({
			.m_TypeName = "PostProcess.FinalColor",
			.m_DisplayName = "Final Color",
			.m_CategoryName = "PostProcess",
			.m_Description = "Applies the final color transform to the presentation target.",
			.m_Category = RenderPassCategory::PostProcess,
			.m_Type = RenderPassType::Graphics,
		}) {}
		~RenderPassFinalColor() override = default;

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
