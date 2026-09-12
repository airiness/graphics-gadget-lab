#pragma once
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderPass/RenderPassIBLBrdfLUT.h"
#include "Graphics/RenderPass/RenderPassIBLClear.h"
#include "Graphics/RenderPass/RenderPassIBLEnvironment.h"
#include "Graphics/RenderPass/RenderPassIBLEnvironmentMipChain.h"
#include "Graphics/RenderPass/RenderPassIBLIrradiance.h"
#include "Graphics/RenderPass/RenderPassIBLPrefilteredSpecular.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderServices.h"

namespace gglab
{
	class RenderPassIBL : public RenderPassBase
	{
	public:
		RenderPassIBL() noexcept :
			RenderPassBase({
				  .m_TypeName = "IBL.SetupResources",
				  .m_DisplayName = "IBL Resource Setup",
				  .m_CategoryName = "IBL",
				  .m_Description =
					  "Imports and publishes the persistent runtime textures used by the IBL passes.",
				  .m_Category = RenderPassCategory::IBL,
				  .m_Type = RenderPassType::Mixed,
				  .m_EnableGpuMarker = false,
				  .m_EnableProfiling = false,
				})
		{
		}

		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;
		void AddFinishPass(RenderGraph& rg) noexcept;

	private:
		static RGTextureId ImportRuntimeTexture(RenderGraph::RGBuilder& builder,
			RenderResourceRegistryAccess& registry, RenderTextureIndex texIndex,
			const char* name, bool initialized, bool bakeTarget = false) noexcept;

	private:
		RenderPassIBLEnvironment m_IBLEnvironmentPass;
		RenderPassIBLEnvironmentMipChain m_IBLEnvironmentMipChainPass;
		RenderPassIBLIrradiance m_IBLIrradiancePass;
		RenderPassIBLPrefilteredSpecular m_IBLPrefilteredSpecularPass;
		RenderPassIBLBrdfLUT m_IBLBrdfLUTPass;
		RenderPassIBLClear m_IBLClearPass;
	};
}
