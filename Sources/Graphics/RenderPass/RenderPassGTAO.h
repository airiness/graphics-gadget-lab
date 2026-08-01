#pragma once

#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RenderPass/RenderPassBase.h"

namespace gglab
{
	class Renderer;

	class RenderPassGTAO final : public RenderPassBase
	{
	public:
		RenderPassGTAO() noexcept :
			RenderPassBase({
				  .m_TypeName = "Lighting.GTAO.Evaluate",
				  .m_DisplayName = "GTAO Evaluate",
				  .m_CategoryName = "Lighting",
				  .m_Description =
					  "Evaluates deterministic half-resolution ambient occlusion from display depth.",
				  .m_Category = RenderPassCategory::Lighting,
				  .m_Type = RenderPassType::Compute,
				})
		{
		}
		~RenderPassGTAO() override = default;

		void Prepare(const RenderServices& services) noexcept;
		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;
		[[nodiscard]] bool IsAvailable() const noexcept { return m_IsAvailable; }

	private:
		[[nodiscard]] RHIPipelineHandle GetOrCreatePipeline(const Renderer& renderer) noexcept;

		ComputePipelineRecipe m_PipelineRecipe{};
		ComputePipelineSlot m_PipelineSlot{};
		bool m_IsInitialized = false;
		bool m_IsAvailable = false;
	};
}
