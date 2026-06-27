#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassIBL.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"

namespace gglab
{
	namespace
	{
		struct PassData {};
	}

	void RenderPassIBL::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* renderResRegistry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(renderResRegistry);

		renderResRegistry->EnsureIblResources();

		rg.AddPass<PassData>("RenderPassIBL.SetupResources", [renderResRegistry](RenderGraph::RGBuilder& builder, PassData&)
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
		const auto* desc = registry.GetTextureDesc(texIndex);
		GGLAB_ASSERT_NOT_NULL(desc);

		return builder.ImportTexture(name, registry.GetTextureHandle(texIndex), *desc, RGTextureAccess::None);
	}
}
