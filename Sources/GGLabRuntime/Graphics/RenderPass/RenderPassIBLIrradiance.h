#pragma once
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderServices.h"

namespace gglab
{
	class RenderPassIBLIrradiance : public RenderPassBase
	{
	public:
		RenderPassIBLIrradiance() noexcept :
			RenderPassBase({
				  .m_TypeName = "IBL.Irradiance",
				  .m_DisplayName = "IBL Irradiance",
				  .m_CategoryName = "IBL",
				  .m_Description =
					  "Convolves the environment cubemap into diffuse irradiance lighting.",
				  .m_Category = RenderPassCategory::IBL,
				  .m_Type = RenderPassType::Graphics,
				})
		{
		}

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
