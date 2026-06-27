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
	namespace
	{
		struct PassData
		{
			RGTextureId m_BackBuffer{};
			RGTextureId m_BrdfLut{};
			RGTextureId m_EnvironmentCubemapPreview{};
			RGTextureId m_PrefilteredSpecularCubemapPreview{};
			RGTextureId m_DirectionalShadowMapPreview{};
			RGTextureViewId m_Rtv{};
		};
	}

	void RenderPassDevelopGui::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_UNUSED(context);

		rg.AddPass<PassData>("RenderPassDevelopGui",
			[](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();

				auto& targetsTable = blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& viewTargets = targetsTable.GetViewTargets(RenderViewID::Main);
				data.m_BackBuffer = builder.Write(viewTargets.m_BackBuffer, RGTextureAccess::RenderTarget);
				data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(data.m_BackBuffer);

				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);
				data.m_BrdfLut = builder.Read(iblRes.m_BrdfLut, RGTextureAccess::Sample);

				auto& iblPreviewRes = blackboard.Get<RGIBLPreviewResources>(IBLPreviewResourcesName);
				data.m_EnvironmentCubemapPreview = builder.Read(
					iblPreviewRes.m_EnvironmentCubemapPreview,
					RGTextureAccess::Sample);
				data.m_PrefilteredSpecularCubemapPreview = builder.Read(
					iblPreviewRes.m_PrefilteredSpecularCubemapPreview,
					RGTextureAccess::Sample);

				auto& shadowRes = blackboard.Get<RGShadowResources>(ShadowResourcesName);
				data.m_DirectionalShadowMapPreview = builder.Read(
					shadowRes.m_DirectionalShadowMapPreview,
					RGTextureAccess::Sample);
			},
			[&services](RGExecuteContext& executeContext, PassData& data)
			{
				GGLAB_UNUSED(data.m_BrdfLut);
				GGLAB_UNUSED(data.m_EnvironmentCubemapPreview);
				GGLAB_UNUSED(data.m_PrefilteredSpecularCubemapPreview);
				GGLAB_UNUSED(data.m_DirectionalShadowMapPreview);

				auto* developGuiBackend = services.m_Renderer->GetDevelopGuiBackend();
				developGuiBackend->RenderDrawData(
					executeContext.GetGraphicsCommandContext(),
					executeContext.GetViewHandle(data.m_Rtv));
			});
	}
}
