#pragma once

#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderPass/RenderPassBase.h"

#include <array>
#include <memory>

namespace gglab
{
	class Renderer;
	class ForwardPlusDebugReadback;

	class RenderPassForwardPlusCull final : public RenderPassBase
	{
	public:
		explicit RenderPassForwardPlusCull(
			std::shared_ptr<ForwardPlusDebugReadback> debugReadback = {}) noexcept :
			RenderPassBase({
				  .m_TypeName = "Lighting.ForwardPlus.Cull",
				  .m_DisplayName = "Forward+ Cull",
				  .m_CategoryName = "Lighting",
				  .m_Description =
					  "Builds deterministic fixed-stride local-light lists from the display depth buffer.",
				  .m_Category = RenderPassCategory::Lighting,
				  .m_Type = RenderPassType::Compute,
				}),
			m_DebugReadback(std::move(debugReadback))
		{
		}
		~RenderPassForwardPlusCull() override = default;

		void Prepare(const RenderServices& services) noexcept;
		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		[[nodiscard]] RHIPipelineHandle GetOrCreatePipeline(
			const Renderer& renderer, bool diagnosticsEnabled) noexcept;

		std::array<ComputePipelineRecipe, 2> m_PipelineRecipes{};
		std::array<ComputePipelineSlot, 2> m_PipelineSlots{};
		std::shared_ptr<ForwardPlusDebugReadback> m_DebugReadback;
		bool m_IsInitialized = false;
	};
}
