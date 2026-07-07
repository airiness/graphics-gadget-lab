#pragma once
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderPass/RenderPassBase.h"

namespace gglab
{
	class RenderPassDebugDrawScene : public RenderPassBase
	{
	public:
		RenderPassDebugDrawScene() noexcept : RenderPassBase({
			.m_TypeName = "Debug.DebugDrawScene",
			.m_DisplayName = "Debug Draw Scene",
			.m_CategoryName = "Debug",
			.m_Description = "Draws depth-tested world-space debug lines into scene color.",
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
