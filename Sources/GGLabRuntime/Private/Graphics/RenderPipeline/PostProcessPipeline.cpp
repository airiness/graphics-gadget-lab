#include "Graphics/RenderPipeline/PostProcessPipeline.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/PostProcess/PostProcessGraphResources.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/Resource/RenderResourceRegistry.h"

namespace gglab
{
	void PostProcessPipeline::AddPasses(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		const RenderViewID displayViewId = context.GetDisplayViewId();
		auto& targets = rg.GetBlackboard()
			.Get<RGViewTargetsTable>(ViewTargetsTableName)
			.GetViewTargets(displayViewId);

		auto& resources =
			rg.GetBlackboard().GetOrCreate<RGPostProcessResources>(PostProcessResourcesName);
		resources = {
			.m_Exposure = context.GetDisplayViewRenderSettings().m_Exposure,
			.m_Inputs =
				{
					.m_SceneColor =
						{
							.m_Texture = targets.m_SceneColor,
							.m_State = PostProcessColorState::SceneLinearRec709,
							.m_PreExposure = context.GetDisplayRenderView().m_ScenePreExposure,
						},
				},
			.m_Output =
				{
					.m_Texture = targets.m_BackBuffer,
					.m_Transform =
						{
							.m_Mode = OutputColorMode::SdrSRGB,
						},
				},
		};
		if (context.GetTemporalFramePlan().m_Active)
		{
			GGLAB_ASSERT_NOT_NULL(context.m_TemporalFrameTransaction);
			GGLAB_ASSERT_MSG(context.m_TemporalFrameTransaction->GetScenePreExposure() ==
				resources.m_Inputs.m_SceneColor.m_PreExposure,
				"Temporal history and SceneColor must use the same storage scale.");
		}

		auto* registry = services.m_Resources;
		GGLAB_ASSERT_NOT_NULL(registry);
		const auto previewSelection = registry->GetPostProcessPreviewSelection();
		const bool wantsIntermediateBloomTap =
			registry->IsPostProcessPreviewRequested() &&
			(previewSelection.m_Tap == PostProcessDebugTap::BloomPrefilter ||
				previewSelection.m_Tap == PostProcessDebugTap::BloomPyramid);
		if (wantsIntermediateBloomTap)
		{
			m_BloomPass.AddPass(rg, context, services,
				[this, &rg, &context, &services](const RGPostProcessColor& source,
					PostProcessDebugTap tap, uint32_t bloomPyramidLevel)
				{
					m_PreviewPass.AddPassForTap(
						rg, context, services, source, tap, bloomPyramidLevel);
				});
		}
		else
		{
			m_BloomPass.AddPass(rg, context, services);
		}
		m_PreviewPass.AddPass(rg, context, services);
		m_FinalColorPass.AddPass(rg, context, services);
		// Publish the version written by FinalColor to downstream presentation passes.
		targets.m_BackBuffer = resources.m_Output.m_Texture;
	}
}
