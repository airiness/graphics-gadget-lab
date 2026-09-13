#pragma once
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphResources.h"

namespace gglab
{
	class RenderPassIBLBrdfLUT : public RenderPassBase
	{
	public:
		RenderPassIBLBrdfLUT() noexcept :
			RenderPassBase({
				  .m_TypeName = "IBL.BrdfLUT",
				  .m_DisplayName = "IBL BRDF LUT",
				  .m_CategoryName = "IBL",
				  .m_Description =
					  "Generates the split-sum BRDF integration lookup texture used by image-based lighting.",
				  .m_Category = RenderPassCategory::IBL,
				  .m_Type = RenderPassType::Graphics,
				})
		{
		}
		~RenderPassIBLBrdfLUT() override = default;

		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		RHIPipelineHandle GetOrCreatePSO(
			const RenderServices& services, RHIFormat renderTargetFormat) noexcept;

	private:
		GraphicsPhysicalPipelineKey m_BaseRecipe{};
		GraphicsPipelineSlot m_PipelineSlot{};
		bool m_IsInitialized = false;
	};
}
