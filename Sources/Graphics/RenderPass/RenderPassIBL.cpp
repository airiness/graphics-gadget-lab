#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassIBL.h"
#include "Graphics/IBLBakeScheduler.h"
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

		auto* bakeScheduler = renderer->GetIBLBakeScheduler();
		GGLAB_ASSERT_NOT_NULL(bakeScheduler);

		rg.AddPass<PassData>(GetRenderGraphPassName(), [renderResRegistry](RenderGraph::RGBuilder& builder, PassData&)
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

				iblRes.m_BakeEnvironmentCubemap = ImportRuntimeTexture(builder,
					*renderResRegistry,
					RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap,
					"IBL.Bake.EnvironmentCubemap",
					true);
				iblRes.m_BakeIrradianceCubemap = ImportRuntimeTexture(builder,
					*renderResRegistry,
					RenderResourceRegistry::TextureIndex::IBL_IrradianceCubemap,
					"IBL.Bake.IrradianceCubemap",
					true);
				iblRes.m_BakePrefilteredSpecularCubemap = ImportRuntimeTexture(builder,
					*renderResRegistry,
					RenderResourceRegistry::TextureIndex::IBL_PrefilteredSpecularCubemap,
					"IBL.Bake.PrefilteredSpecularCubemap",
					true);
				iblRes.m_BakeBrdfLut = ImportRuntimeTexture(builder,
					*renderResRegistry,
					RenderResourceRegistry::TextureIndex::IBL_BrdfLut,
					"IBL.Bake.BrdfLut",
					true);

			});

		m_IBLClearPass.AddPass(rg, context, services);

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
		struct FinishPassData {};
		rg.AddPass<FinishPassData>("IBL.FinishResources",
			[](RenderGraph::RGBuilder& builder, FinishPassData&)
			{
				builder.SideEffect();
				auto& ibl = builder.GetBlackboard().Get<RGIBLResources>(IBLResourcesName);
				builder.Export(ibl.m_EnvironmentCubemap, RGTextureAccess::None);
				builder.Export(ibl.m_IrradianceCubemap, RGTextureAccess::None);
				builder.Export(ibl.m_PrefilteredSpecularCubemap, RGTextureAccess::None);
				builder.Export(ibl.m_BrdfLut, RGTextureAccess::None);
				builder.Export(ibl.m_BakeEnvironmentCubemap, RGTextureAccess::None);
				builder.Export(ibl.m_BakeIrradianceCubemap, RGTextureAccess::None);
				builder.Export(ibl.m_BakePrefilteredSpecularCubemap, RGTextureAccess::None);
				builder.Export(ibl.m_BakeBrdfLut, RGTextureAccess::None);

				auto& previews = builder.GetBlackboard().Get<RGIBLPreviewResources>(IBLPreviewResourcesName);
				builder.Export(previews.m_EnvironmentCubemapPreview, RGTextureAccess::None);
				builder.Export(previews.m_IrradianceCubemapPreview, RGTextureAccess::None);
				builder.Export(previews.m_PrefilteredSpecularCubemapPreview, RGTextureAccess::None);
			},
			[](RGExecuteContext&, FinishPassData&) {});
	}

	RGTextureId RenderPassIBL::ImportRuntimeTexture(RenderGraph::RGBuilder& builder,
		RenderResourceRegistry& registry,
		RenderResourceRegistry::TextureIndex texIndex,
		const char* name,
		bool bakeTarget) noexcept
	{
		const auto* desc = bakeTarget ?
			registry.GetIBLBakeTextureDesc(texIndex) : registry.GetTextureDesc(texIndex);
		GGLAB_ASSERT_NOT_NULL(desc);

		return builder.ImportTexture(
			name,
			bakeTarget ? registry.GetIBLBakeTextureHandle(texIndex) : registry.GetTextureHandle(texIndex),
			*desc,
			RGTextureAccess::None);
	}
}
