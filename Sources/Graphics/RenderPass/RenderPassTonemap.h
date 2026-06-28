#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/PipelineCache.h"

namespace gglab
{
	class Renderer;

	class RenderPassTonemap final : public RenderPassBase
	{
	public:
		RenderPassTonemap() noexcept : RenderPassBase({
			.m_TypeName = "PostProcess.Tonemap",
			.m_DisplayName = "Tonemap",
			.m_CategoryName = "PostProcess",
			.m_Description = "Maps HDR scene color to the display back buffer.",
			.m_Category = RenderPassCategory::PostProcess,
			.m_Type = RenderPassType::Graphics,
		}) {}
		~RenderPassTonemap() override = default;

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
