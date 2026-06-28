#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderPass/RenderPassIBLBrdfLUT.h"
#include "Graphics/RenderPass/RenderPassIBLEnvironment.h"
#include "Graphics/RenderPass/RenderPassIBLIrradiance.h"
#include "Graphics/RenderPass/RenderPassIBLPrefilteredSpecular.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Resource/RenderResourceRegistry.h"

namespace gglab
{
	class RenderPassIBL : public RenderPassBase
	{
	public:
		RenderPassIBL() noexcept : RenderPassBase({
			.m_TypeName = "IBL.SetupResources",
			.m_DisplayName = "IBL Resource Setup",
			.m_CategoryName = "IBL",
			.m_Description = "Imports and publishes the persistent runtime textures used by the IBL passes.",
			.m_Category = RenderPassCategory::IBL,
			.m_Type = RenderPassType::Mixed,
			.m_EnableGpuMarker = false,
			.m_EnableProfiling = false,
		}) {}

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		static RGTextureId ImportRuntimeTexture(RenderGraph::RGBuilder& builder,
			RenderResourceRegistry& registry,
			RenderResourceRegistry::TextureIndex texIndex,
			const char* name) noexcept;

	private:
		RenderPassIBLEnvironment m_IBLEnvironmentPass;
		RenderPassIBLIrradiance m_IBLIrradiancePass;
		RenderPassIBLPrefilteredSpecular m_IBLPrefilteredSpecularPass;
		RenderPassIBLBrdfLUT m_IBLBrdfLUTPass;
	};
}
