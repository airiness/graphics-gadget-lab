#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassDevelopGui.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RenderPass/IBLGraphResources.h"
#include "Graphics/RenderPass/ShadowGraphResources.h"
#include "DevTools/DevelopGui/DevelopGuiSystem.h"

namespace gglab
{
	namespace
	{
		struct PassData
		{
			RGTextureId m_BackBuffer{};
			RGTextureId m_BrdfLut{};
			RGTextureId m_EnvironmentCubemapPreview{};
			RGTextureId m_IrradianceCubemapPreview{};
			RGTextureId m_PrefilteredSpecularCubemapPreview{};
			RGTextureId m_DirectionalShadowMapPreview{};
			RGTextureViewId m_Rtv{};
		};
	}

	void RenderPassDevelopGui::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		auto* developGuiSystem = services.m_DevelopGuiSystem;
		if (!developGuiSystem || !developGuiSystem->IsFrameOpen())
		{
			return;
		}
		const RenderViewID displayViewId = context.GetDisplayViewId();

		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[displayViewId](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();

				auto& targetsTable =
					blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& viewTargets = targetsTable.GetViewTargets(displayViewId);
				builder.ReadWriteInPlace(viewTargets.m_BackBuffer, RGTextureAccess::RenderTarget);
				data.m_BackBuffer = viewTargets.m_BackBuffer;
				data.m_Rtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_BackBuffer);

				if (const auto* iblRes = blackboard.TryGet<RGIBLResources>(IBLResourcesName);
					iblRes && iblRes->m_BrdfLut.IsValid())
				{
					data.m_BrdfLut = builder.Read(iblRes->m_BrdfLut, RGTextureAccess::Sample);
				}

				if (const auto* iblPreviewRes =
					blackboard.TryGet<RGIBLPreviewResources>(IBLPreviewResourcesName))
				{
					if (iblPreviewRes->m_EnvironmentCubemapPreview.IsValid())
					{
						data.m_EnvironmentCubemapPreview = builder.Read(
							iblPreviewRes->m_EnvironmentCubemapPreview, RGTextureAccess::Sample);
					}
					if (iblPreviewRes->m_IrradianceCubemapPreview.IsValid())
					{
						data.m_IrradianceCubemapPreview = builder.Read(
							iblPreviewRes->m_IrradianceCubemapPreview, RGTextureAccess::Sample);
					}
					if (iblPreviewRes->m_PrefilteredSpecularCubemapPreview.IsValid())
					{
						data.m_PrefilteredSpecularCubemapPreview =
							builder.Read(iblPreviewRes->m_PrefilteredSpecularCubemapPreview,
								RGTextureAccess::Sample);
					}
				}

				if (const auto* shadowRes =
					blackboard.TryGet<RGShadowResources>(ShadowResourcesName);
					shadowRes && shadowRes->m_DirectionalShadowMapPreview.IsValid())
				{
					data.m_DirectionalShadowMapPreview = builder.Read(
						shadowRes->m_DirectionalShadowMapPreview, RGTextureAccess::Sample);
				}
			},
			[developGuiSystem](RGExecuteContext& executeContext, PassData& data)
			{
				GGLAB_UNUSED(data.m_BrdfLut);
				GGLAB_UNUSED(data.m_EnvironmentCubemapPreview);
				GGLAB_UNUSED(data.m_IrradianceCubemapPreview);
				GGLAB_UNUSED(data.m_PrefilteredSpecularCubemapPreview);
				GGLAB_UNUSED(data.m_DirectionalShadowMapPreview);

				if (!developGuiSystem->IsFrameOpen())
				{
					return;
				}

				developGuiSystem->RenderDrawData(executeContext.GetGraphicsCommandContext(),
					executeContext.GetViewHandle(data.m_Rtv));
			});
	}
}
