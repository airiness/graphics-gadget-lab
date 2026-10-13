#pragma once

#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAOTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"

namespace gglab
{
	// Evaluation-only supersampled reference accumulation. In a frame that carries a
	// temporal reference sample, adds the HDR scene color to the persistent sum and
	// replaces the display view's scene color with the running mean before
	// post-processing. An ambient-occlusion reference accumulates the final GTAO
	// visibility and publishes its mean in RGTemporalReferenceResources instead.
	class RenderPassTemporalReference final : public RenderPassBase
	{
	public:
		RenderPassTemporalReference() noexcept :
			RenderPassBase({
				.m_TypeName = "PostProcess.TemporalReference",
				.m_DisplayName = "Temporal Reference",
				.m_CategoryName = "Post Process",
				.m_Description =
					"Accumulates jittered scene color into a supersampled reference mean.",
				.m_Category = RenderPassCategory::PostProcess,
				.m_Type = RenderPassType::Compute,
			})
		{
		}
		~RenderPassTemporalReference() override = default;

		void Prepare(const RenderServices& services) noexcept;
		[[nodiscard]] static RenderFrameValidationResult ValidateSource(
			TemporalReferenceSignal signal, GTAOFrameStatus gtaoStatus) noexcept;
		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;
		[[nodiscard]] bool ValidatePipelineClosure(const RenderServices& services) noexcept;

	private:
		[[nodiscard]] RHIPipelineHandle GetOrCreatePipeline(const RenderServices& services) noexcept;

		ComputePipelineRecipe m_PipelineRecipe{};
		ComputePipelineSlot m_PipelineSlot{};
		bool m_IsInitialized = false;
		bool m_IsAvailable = false;
	};
}
