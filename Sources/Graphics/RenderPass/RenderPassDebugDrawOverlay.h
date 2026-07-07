#pragma once
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderPass/RenderPassBase.h"

namespace gglab
{
	class RenderPassDebugDrawOverlay : public RenderPassBase
	{
	public:
		RenderPassDebugDrawOverlay() noexcept : RenderPassBase({
			.m_TypeName = "Debug.DebugDrawOverlay",
			.m_DisplayName = "Debug Draw Overlay",
			.m_CategoryName = "Debug",
			.m_Description = "Draws always-visible world and screen-space debug lines.",
			.m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Graphics,
		}) {}

		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		GraphicsPipelineRecipe m_Recipe{};
		GraphicsPipelineSlot m_PipelineSlot{};
		bool m_IsInitialized = false;
	};
}
