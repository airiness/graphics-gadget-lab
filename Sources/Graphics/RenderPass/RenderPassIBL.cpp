#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassIBL.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGResourceUtils.h"

namespace gglab
{
	void RenderPassIBL::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* renderResRegistry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(renderResRegistry);

		renderResRegistry->EnsureIblResources();

		struct SetupData {};
		rg.AddPass<SetupData>("RenderPassIBL.SetupResources", [renderResRegistry](RenderGraph::RGBuilder& builder, SetupData&)
			{
				builder.SideEffect();

				auto& iblRes = builder.GetBlackboard().GetOrCreate<RGIBLResources>(IBLResourcesName);

				iblRes.m_EnvironmentCubemap = ImportRuntimeTexture(builder,
					*renderResRegistry,
					RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap,
					"IBL.EnvironmentCubemap");

				iblRes.m_IrradianceCubemap = ImportRuntimeTexture(builder,
					*renderResRegistry,
					RenderResourceRegistry::TextureIndex::IBL_IrradianceCubemap,
					"IBL.IrradianceCubemap");

				iblRes.m_PrefilteredSpecularCubemap = ImportRuntimeTexture(builder,
					*renderResRegistry,
					RenderResourceRegistry::TextureIndex::IBL_PrefilteredSpecularCubemap,
					"IBL.PrefilteredSpecularCubemap");

				iblRes.m_BrdfLut = ImportRuntimeTexture(builder,
					*renderResRegistry,
					RenderResourceRegistry::TextureIndex::IBL_BrdfLut,
					"IBL.BrdfLut");
			});

		// Add IBL Environment Pass
		m_IBLEnvironmentPass.AddPass(rg, context, services);

		// Add IBL Irradiance Pass
		m_IBLIrradiancePass.AddPass(rg, context, services);

		// Add IBL Prefiltered Specular Pass
		m_IBLPrefilteredSpecularPass.AddPass(rg, context, services);

		// Add IBL BRDF LUT Pass
		m_IBLBrdfLUTPass.AddPass(rg, context, services);
	}

	RGTextureId RenderPassIBL::ImportRuntimeTexture(RenderGraph::RGBuilder& builder,
		RenderResourceRegistry& registry,
		RenderResourceRegistry::TextureIndex texIndex,
		const char* name) noexcept
	{
		auto* texture = registry.GetTexture(texIndex);
		GGLAB_ASSERT_NOT_NULL(texture);

		const auto desc = ToRGTextureDesc(*texture, RGTextureUsage::RenderTarget | RGTextureUsage::Sample);
		return builder.ImportTexture(name, texture, desc, D3D12_RESOURCE_STATE_COMMON);
	}
}
