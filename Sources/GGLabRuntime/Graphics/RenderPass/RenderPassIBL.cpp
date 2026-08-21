#include "Graphics/RenderPass/RenderPassIBL.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"

namespace gglab
{
	namespace
	{
		struct PassData
		{
		};
	}

	void RenderPassIBL::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* renderResRegistry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(renderResRegistry);

		auto* bakeScheduler = renderer->GetIBLBakeScheduler();
		GGLAB_ASSERT_NOT_NULL(bakeScheduler);
		const bool activeResourcesInitialized = renderResRegistry->HasInitializedActiveIBL();
		const bool bakeResourcesInitialized = !bakeScheduler->ShouldInitializeBakeResources();

		rg.AddPass<PassData>(GetRenderGraphPassName(),
			[renderResRegistry, activeResourcesInitialized, bakeResourcesInitialized](
				RenderGraph::RGBuilder& builder, PassData&)
			{
				builder.SideEffect();

				auto& iblRes =
					builder.GetBlackboard().GetOrCreate<RGIBLResources>(IBLResourcesName);

				iblRes.m_EnvironmentCubemap = ImportRuntimeTexture(builder, *renderResRegistry,
					RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap,
					"IBL.EnvironmentCubemap", activeResourcesInitialized);

				iblRes.m_IrradianceCubemap = ImportRuntimeTexture(builder, *renderResRegistry,
					RenderResourceRegistry::TextureIndex::IBL_IrradianceCubemap,
					"IBL.IrradianceCubemap", activeResourcesInitialized);

				iblRes.m_PrefilteredSpecularCubemap =
					ImportRuntimeTexture(builder, *renderResRegistry,
						RenderResourceRegistry::TextureIndex::IBL_PrefilteredSpecularCubemap,
						"IBL.PrefilteredSpecularCubemap", activeResourcesInitialized);

				iblRes.m_BrdfLut = ImportRuntimeTexture(builder, *renderResRegistry,
					RenderResourceRegistry::TextureIndex::IBL_BrdfLut, "IBL.BrdfLut",
					activeResourcesInitialized);

				if (renderResRegistry->HasIBLBakeResources())
				{
					iblRes.m_BakeEnvironmentCubemap =
						ImportRuntimeTexture(builder, *renderResRegistry,
							RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap,
							"IBL.Bake.EnvironmentCubemap", bakeResourcesInitialized, true);
					iblRes.m_BakeIrradianceCubemap =
						ImportRuntimeTexture(builder, *renderResRegistry,
							RenderResourceRegistry::TextureIndex::IBL_IrradianceCubemap,
							"IBL.Bake.IrradianceCubemap", bakeResourcesInitialized, true);
					iblRes.m_BakePrefilteredSpecularCubemap =
						ImportRuntimeTexture(builder, *renderResRegistry,
							RenderResourceRegistry::TextureIndex::IBL_PrefilteredSpecularCubemap,
							"IBL.Bake.PrefilteredSpecularCubemap", bakeResourcesInitialized, true);
					iblRes.m_BakeBrdfLut = ImportRuntimeTexture(builder, *renderResRegistry,
						RenderResourceRegistry::TextureIndex::IBL_BrdfLut, "IBL.Bake.BrdfLut",
						bakeResourcesInitialized, true);
				}

				auto& previewResources = builder.GetBlackboard().GetOrCreate<RGIBLPreviewResources>(
					IBLPreviewResourcesName);
				auto importPreview =
					[&builder, renderResRegistry, activeResourcesInitialized](
						RenderResourceRegistry::TextureIndex index, const char* name) noexcept
					{
						const auto* desc = renderResRegistry->GetTextureDesc(index);
						GGLAB_ASSERT_NOT_NULL(desc);
						const RGPersistentTextureImportContract importContract =
							ResolveRGPersistentTextureImportContract(activeResourcesInitialized);
						return builder.ImportTexture(name, renderResRegistry->GetTextureHandle(index),
							*desc, importContract.m_InitialState,
							importContract.m_InitialContentValidity);
					};
				previewResources.m_EnvironmentCubemapPreview = importPreview(
					RenderResourceRegistry::TextureIndex::Preview_IBL_EnvironmentCubemap,
					"Preview.IBL.EnvironmentCubemap");
				previewResources.m_IrradianceCubemapPreview = importPreview(
					RenderResourceRegistry::TextureIndex::Preview_IBL_IrradianceCubemap,
					"Preview.IBL.IrradianceCubemap");
				previewResources.m_PrefilteredSpecularCubemapPreview = importPreview(
					RenderResourceRegistry::TextureIndex::Preview_IBL_PrefilteredSpecularCubemap,
					"Preview.IBL.PrefilteredSpecularCubemap");
			});

		m_IBLClearPass.AddPass(rg, context, services);
		m_IBLClearPass.AddBakePass(rg, context, services);

		switch (bakeScheduler->GetStageForRecording())
		{
		case IBLBakeStage::Environment:
			m_IBLEnvironmentPass.AddPass(rg, context, services);
			break;
		case IBLBakeStage::EnvironmentMipChain:
			m_IBLEnvironmentMipChainPass.AddPass(rg, context, services);
			break;
		case IBLBakeStage::Irradiance:
			m_IBLIrradiancePass.AddPass(rg, context, services);
			break;
		case IBLBakeStage::PrefilteredSpecular:
			m_IBLPrefilteredSpecularPass.AddPass(rg, context, services);
			break;
		case IBLBakeStage::BrdfLut:
			m_IBLBrdfLUTPass.AddPass(rg, context, services);
			break;
		default:
			break;
		}
	}

	void RenderPassIBL::AddFinishPass(RenderGraph& rg) noexcept
	{
		struct FinishPassData
		{
		};
		rg.AddPass<FinishPassData>(
			"IBL.FinishResources",
			[](RenderGraph::RGBuilder& builder, FinishPassData&)
			{
				builder.SideEffect();
				auto& ibl = builder.GetBlackboard().Get<RGIBLResources>(IBLResourcesName);
				builder.Export(ibl.m_EnvironmentCubemap, RGTextureAccess::None);
				builder.Export(ibl.m_IrradianceCubemap, RGTextureAccess::None);
				builder.Export(ibl.m_PrefilteredSpecularCubemap, RGTextureAccess::None);
				builder.Export(ibl.m_BrdfLut, RGTextureAccess::None);
				if (ibl.m_BakeEnvironmentCubemap.IsValid())
				{
					builder.Export(ibl.m_BakeEnvironmentCubemap, RGTextureAccess::None);
					builder.Export(ibl.m_BakeIrradianceCubemap, RGTextureAccess::None);
					builder.Export(ibl.m_BakePrefilteredSpecularCubemap, RGTextureAccess::None);
					builder.Export(ibl.m_BakeBrdfLut, RGTextureAccess::None);
				}

				auto& previews =
					builder.GetBlackboard().Get<RGIBLPreviewResources>(IBLPreviewResourcesName);
				builder.Export(previews.m_EnvironmentCubemapPreview, RGTextureAccess::None);
				builder.Export(previews.m_IrradianceCubemapPreview, RGTextureAccess::None);
				builder.Export(previews.m_PrefilteredSpecularCubemapPreview, RGTextureAccess::None);
			},
			[](RGExecuteContext&, FinishPassData&) {});
	}

	RGTextureId RenderPassIBL::ImportRuntimeTexture(RenderGraph::RGBuilder& builder,
		RenderResourceRegistry& registry, RenderResourceRegistry::TextureIndex texIndex,
		const char* name, bool initialized, bool bakeTarget) noexcept
	{
		const auto* desc = bakeTarget ? registry.GetIBLBakeTextureDesc(texIndex)
			: registry.GetTextureDesc(texIndex);
		GGLAB_ASSERT_NOT_NULL(desc);

		const RGPersistentTextureImportContract importContract =
			ResolveRGPersistentTextureImportContract(initialized);
		return builder.ImportTexture(name,
			bakeTarget ? registry.GetIBLBakeTextureHandle(texIndex)
			: registry.GetTextureHandle(texIndex),
			*desc, importContract.m_InitialState, importContract.m_InitialContentValidity);
	}
}
