#pragma once
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderPass/RenderPassBase.h"

namespace gglab
{
	enum class DebugDrawPassMode : uint8_t
	{
		Scene,
		Overlay,
	};

	class RenderPassDebugDraw : public RenderPassBase
	{
	public:
		explicit RenderPassDebugDraw(DebugDrawPassMode mode) noexcept;

		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		static RenderPassInfo MakeInfo(DebugDrawPassMode mode) noexcept;
		void EnsureInitialized(const RenderServices& services) noexcept;
		RHIPipelineHandle GetPipeline(const Renderer& renderer, bool triangles) noexcept;

		DebugDrawPassMode m_Mode = DebugDrawPassMode::Scene;
		std::array<GraphicsPipelineRecipe, 2> m_Recipes{};
		std::array<GraphicsPipelineSlot, 2> m_PipelineSlots{};
		bool m_IsInitialized = false;
	};
}
