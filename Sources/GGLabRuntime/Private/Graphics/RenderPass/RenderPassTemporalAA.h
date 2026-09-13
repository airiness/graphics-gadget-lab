#pragma once

#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"

namespace gglab
{

	class RenderPassTemporalAA final : public RenderPassBase
	{
	public:
		RenderPassTemporalAA() noexcept :
			RenderPassBase({
				.m_TypeName = "PostProcess.TemporalAA",
				.m_DisplayName = "Temporal AA",
				.m_CategoryName = "Post Process",
				.m_Description =
					"Reprojects opaque history and rejects incompatible previous surfaces.",
				.m_Category = RenderPassCategory::PostProcess,
				.m_Type = RenderPassType::Compute,
			})
		{
		}
		~RenderPassTemporalAA() override = default;

		void Prepare(const RenderServices& services) noexcept;
		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;
		[[nodiscard]] bool IsAvailable() const noexcept { return m_IsAvailable; }
		[[nodiscard]] bool ValidatePipelineClosure(const RenderServices& services) noexcept;

	private:
		[[nodiscard]] RHIPipelineHandle GetOrCreatePipeline(const RenderServices& services) noexcept;

		ComputePipelineRecipe m_PipelineRecipe{};
		ComputePipelineSlot m_PipelineSlot{};
		bool m_IsInitialized = false;
		bool m_IsAvailable = false;
	};
}
