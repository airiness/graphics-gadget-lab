#include "Graphics/RenderPipeline/PostProcessPipeline.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/PostProcess/PostProcessGraphResources.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureAccess.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/RenderServices.h"

#include <format>
#include <optional>

namespace gglab
{
	void PostProcessPipeline::AddDiagnosticCapturePass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		RenderFrameCaptureAccess* capture = services.m_FrameCapture;
		const std::optional<PostProcessDebugTap> tap =
			capture ? capture->GetPendingDiagnosticTap() : std::nullopt;
		if (!tap || m_PreviewPass.AddDiagnosticCapturePass(rg, context, services, *tap))
		{
			return;
		}
		capture->FailPendingDiagnosticRequests(std::format(
			"Diagnostic tap '{}' has no source in this frame; its feature may be disabled.",
			GetFrameCaptureDiagnosticTapName(*tap)));
	}

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
							.m_Texture = targets.m_DisplayColor,
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
		bool wantsIntermediateBloomTap = false;
		for (uint32_t index = 0; index < static_cast<uint32_t>(PostProcessPreviewChannel::Count); ++index)
		{
			const auto channel = static_cast<PostProcessPreviewChannel>(index);
			if (!registry->IsPostProcessPreviewRequested(channel)) continue;
			const auto tap = registry->GetPostProcessPreviewSelection(channel).m_Tap;
			wantsIntermediateBloomTap |= tap == PostProcessDebugTap::BloomPrefilter ||
				tap == PostProcessDebugTap::BloomPyramid;
		}
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
