#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassDevelopGui.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGFrameTargets.h"
#include "Graphics/RenderGraph/RGIBLResources.h"
#include "Graphics/RenderGraph/RGIBLPreviewResources.h"
#include "Graphics/RenderGraph/RGShadowResources.h"
#include "DevTools/DevelopGui/DevelopGuiBackend.h"

namespace gglab
{
	void RenderPassDevelopGui::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_UNUSED(context);

		struct DevelopGuiData
		{
			RGTextureId m_BackBuffer{};
			RGTextureId m_BrdfLut{};
			RGTextureId m_EnvironmentCubemapPreview{};
			RGTextureId m_PrefilteredSpecularCubemapPreview{};
			RGTextureId m_DirectionalShadowMapPreview{};
			ViewKey m_RtvKey{};
		};

		rg.AddPass<DevelopGuiData>("RenderPassDevelopGui",
			[](RenderGraph::RGBuilder& builder, DevelopGuiData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();

				auto& targetsTable = blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& viewTargets = targetsTable.GetViewTargets(RenderViewID::Main);
				data.m_BackBuffer = builder.Write(viewTargets.m_BackBuffer, RGTextureUsage::RenderTarget);
				data.m_RtvKey = viewTargets.m_BackBufferRTVKey;

				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);
				data.m_BrdfLut = builder.Read(iblRes.m_BrdfLut, RGTextureUsage::Sample);

				auto& iblPreviewRes = blackboard.Get<RGIBLPreviewResources>(IBLPreviewResourcesName);
				data.m_EnvironmentCubemapPreview = builder.Read(
					iblPreviewRes.m_EnvironmentCubemapPreview,
					RGTextureUsage::Sample);
				data.m_PrefilteredSpecularCubemapPreview = builder.Read(
					iblPreviewRes.m_PrefilteredSpecularCubemapPreview,
					RGTextureUsage::Sample);

				auto& shadowRes = blackboard.Get<RGShadowResources>(ShadowResourcesName);
				data.m_DirectionalShadowMapPreview = builder.Read(
					shadowRes.m_DirectionalShadowMapPreview,
					RGTextureUsage::Sample);
			},
			[&rg, &services](RGExecuteContext& executeContext, DevelopGuiData& data)
			{
				GGLAB_UNUSED(data.m_BrdfLut);
				GGLAB_UNUSED(data.m_EnvironmentCubemapPreview);
				GGLAB_UNUSED(data.m_PrefilteredSpecularCubemapPreview);
				GGLAB_UNUSED(data.m_DirectionalShadowMapPreview);

				auto* developGuiBackend = services.m_Renderer->GetDevelopGuiBackend();

				auto* backTexture = rg.GetTexture(data.m_BackBuffer);
				auto* viewCache = rg.GetViewCache();
				const auto& rtv = viewCache->GetOrCreate(data.m_RtvKey, backTexture);
				developGuiBackend->RenderDrawData(executeContext.m_GraphicsCommandList, rtv);
			});
	}
}
