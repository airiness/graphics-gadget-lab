#pragma once
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"

namespace gglab
{

	class RenderPassIBLEnvironmentMipChain : public RenderPassBase
	{
	public:
		RenderPassIBLEnvironmentMipChain() noexcept :
			RenderPassBase({
				  .m_TypeName = "IBL.EnvironmentMipChain",
				  .m_DisplayName = "IBL Environment Mip Chain",
				  .m_CategoryName = "IBL",
				  .m_Description =
					  "Builds the seam-aware mip chain used when filtering the environment cubemap.",
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

		GraphicsPhysicalPipelineKey m_BaseRecipe{};
		GraphicsPipelineSlot m_PipelineSlot{};
		bool m_IsInitialized = false;
	};
}
