#pragma once

#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderPass/RenderPassBase.h"

#include <memory>

namespace gglab
{
	class ForwardPlusDebugReadback;

	class RenderPassForwardPlusValidation final : public RenderPassBase
	{
	public:
		explicit RenderPassForwardPlusValidation(
			std::shared_ptr<ForwardPlusDebugReadback> debugReadback = {}) noexcept :
			RenderPassBase({
				.m_TypeName = "Lighting.ForwardPlus.Validate",
				.m_DisplayName = "Forward+ HDR Diff",
				.m_CategoryName = "Lighting",
				.m_Description =
					"Compares opaque Forward+ and legacy HDR shading with deterministic GPU reduction.",
				.m_Category = RenderPassCategory::Lighting,
				.m_Type = RenderPassType::Compute,
				}),
			m_DebugReadback(std::move(debugReadback))
		{
		}
		~RenderPassForwardPlusValidation() override = default;

		void Prepare(const RenderServices& services) noexcept;
		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;
		[[nodiscard]] bool IsAvailable() const noexcept
		{
			return m_DebugReadback != nullptr;
		}

	private:
		[[nodiscard]] RHIPipelineHandle GetOrCreateTilePipeline(const Renderer& renderer) noexcept;
		[[nodiscard]] RHIPipelineHandle GetOrCreateFramePipeline(const Renderer& renderer) noexcept;

		std::shared_ptr<ForwardPlusDebugReadback> m_DebugReadback;
		ComputePipelineRecipe m_TilePipelineRecipe{};
		ComputePipelineRecipe m_FramePipelineRecipe{};
		ComputePipelineSlot m_TilePipelineSlot{};
		ComputePipelineSlot m_FramePipelineSlot{};
		bool m_IsInitialized = false;
	};
}
