#pragma once
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"

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
		RHIPipelineHandle GetPipeline(const RenderServices& services, bool triangles) noexcept;

		DebugDrawPassMode m_Mode = DebugDrawPassMode::Scene;
		std::array<GraphicsPhysicalPipelineKey, 2> m_Recipes{};
		std::array<GraphicsPipelineSlot, 2> m_PipelineSlots{};
		bool m_IsInitialized = false;
	};
}
