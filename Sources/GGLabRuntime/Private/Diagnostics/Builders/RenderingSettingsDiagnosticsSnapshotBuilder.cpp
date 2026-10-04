#include "Diagnostics/Builders/RenderingSettingsDiagnosticsSnapshotBuilder.h"

#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsSession.h"
#include "GGLabRuntime/Graphics/DirectionalShadowFramePlan.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/ShadowGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "Graphics/PostProcess/PostProcessGraphResources.h"
#include "Graphics/RenderPass/ForwardPlusGraphResources.h"
#include "Graphics/RenderPass/GTAOGraphResources.h"
#include "Graphics/RenderPass/TemporalAAGraphResources.h"

#include <cstddef>

namespace gglab
{
	class RenderingSettingsDiagnosticsSnapshotBuilder
	{
	public:
		static RenderingSettingsDiagnosticsSnapshot Build(const DiagnosticsFrameContext& context,
			const TemporalHistorySummary* history) noexcept
		{
			RenderingSettingsDiagnosticsSnapshot snapshot{};
			snapshot.m_DisplayViewId = context.m_DisplayViewId;
			snapshot.m_FrameSerial = context.m_FrameSerial;
			if (context.m_AuthoringViewRenderProfile)
			{
				snapshot.m_AuthoringProfile = *context.m_AuthoringViewRenderProfile;
			}
			if (context.m_EffectiveViewRenderProfile)
			{
				snapshot.m_RequestedProfile = *context.m_EffectiveViewRenderProfile;
			}
			if (context.m_DisplayViewSettings)
			{
				snapshot.m_ResolvedSettings = *context.m_DisplayViewSettings;
			}
			snapshot.m_SettingsAvailable = context.m_AuthoringViewRenderProfile &&
				context.m_EffectiveViewRenderProfile && context.m_DisplayViewSettings;
			const size_t viewIndex = static_cast<size_t>(context.m_DisplayViewId);
			const RenderView* view = viewIndex < context.m_RenderViews.size()
				? &context.m_RenderViews[viewIndex] : nullptr;
			if (view && view->m_ViewId == context.m_DisplayViewId && view->m_IsValid)
			{
				snapshot.m_Width = view->m_Width;
				snapshot.m_Height = view->m_Height;
			}
			snapshot.m_RuntimeAvailable = snapshot.m_SettingsAvailable &&
				(context.m_DisplayViewId == RenderViewID::Main ||
					IsDebugCameraRenderViewID(context.m_DisplayViewId)) &&
				snapshot.m_Width > 0 && snapshot.m_Height > 0 && context.m_RenderGraph &&
				context.m_RenderGraph->GetExecutionPlan();
			if (!snapshot.m_RuntimeAvailable)
			{
				return snapshot;
			}

			const RenderGraph& graph = *context.m_RenderGraph;
			const auto& blackboard = graph.GetBlackboard();
			const auto* forward = blackboard.TryGet<RGForwardPlusResources>(ForwardPlusResourcesName);
			if (forward)
			{
				snapshot.m_ForwardLighting = ResolveForwardLightingStatus(forward->m_Status);
				if (forward->m_Status == ForwardPlusFrameStatus::Active)
				{
					snapshot.m_ForwardLighting = CheckResourceActivity(graph, forward->m_TileLightHeaders);
				}
			}
			const auto* gtao = blackboard.TryGet<RGGTAOResources>(GTAOResourcesName);
			if (gtao)
			{
				snapshot.m_GTAO = ResolveGTAOStatus(gtao->m_Status);
				if (gtao->m_Status == GTAOFrameStatus::Active)
				{
					snapshot.m_GTAO = CheckResourceActivity(graph, gtao->m_FinalAO);
				}
				snapshot.m_GTAOUsesFormatFallback = gtao->m_FinalAOFormat == RHIFormat::R16Float &&
					snapshot.m_ResolvedSettings.m_Lighting.m_GTAO.m_FinalAOFormatPreference ==
						GTAOFinalAOFormatPreference::PreferR8Unorm;
			}
			if (context.m_TemporalFramePlan &&
				context.m_TemporalFramePlan->m_DisplayViewId == context.m_DisplayViewId)
			{
				const auto& plan = *context.m_TemporalFramePlan;
				snapshot.m_TemporalAA = ResolveTemporalStatus(plan);
				if (plan.m_Active)
				{
					const auto* temporal = blackboard.TryGet<RGTemporalAAResources>(TemporalAAResourcesName);
					snapshot.m_TemporalAA = temporal
						? CheckResourceActivity(graph, temporal->m_ResolvedSceneColor)
						: ViewRenderFeatureStatus{ ViewRenderFeatureState::Inactive, ViewRenderFeatureReason::ResourcesUnavailable };
				}
				if (history && (!history->m_HasActiveHistory ||
					(history->m_DisplayViewId == plan.m_DisplayViewId &&
						history->m_SessionIdentity == plan.m_SessionIdentity)))
				{
					snapshot.m_History = *history;
					snapshot.m_HistoryAvailable = true;
				}
			}

			const auto* postProcess = blackboard.TryGet<RGPostProcessResources>(PostProcessResourcesName);
			if (postProcess)
			{
				snapshot.m_ToneMapping = CheckResourceActivity(graph, postProcess->m_Output.m_Texture);
				snapshot.m_ScenePreExposure = snapshot.m_ToneMapping;
				if (snapshot.m_ScenePreExposure.m_State == ViewRenderFeatureState::Active &&
					!snapshot.m_RequestedProfile.m_EnableScenePreExposure)
				{
					snapshot.m_ScenePreExposure = DisabledStatus();
				}
				const auto& settings = snapshot.m_ResolvedSettings.m_PostProcess.m_Bloom;
				if (!settings.m_Enabled)
				{
					snapshot.m_Bloom = DisabledStatus();
				}
				else if (settings.m_Intensity <= 0.0f)
				{
					snapshot.m_Bloom = { ViewRenderFeatureState::Inactive, ViewRenderFeatureReason::ZeroIntensity };
				}
				else if (!postProcess->m_BloomContributionEnabled)
				{
					const auto* targets = blackboard.TryGet<RGViewTargetsTable>(ViewTargetsTableName);
					const bool materialDiagnostics = targets && targets->GetViewTargets(context.m_DisplayViewId)
						.m_MaterialDiagnosticColor.IsValid();
					snapshot.m_Bloom = { ViewRenderFeatureState::Inactive, materialDiagnostics
						? ViewRenderFeatureReason::MaterialDiagnosticsActive : ViewRenderFeatureReason::ResourcesUnavailable };
				}
				else
				{
					snapshot.m_Bloom = CheckResourceActivity(graph, postProcess->m_Bloom.m_Result.m_Texture);
				}
			}
			if (context.m_DirectionalShadowFramePlan)
			{
				snapshot.m_ShadowSettings = context.m_DirectionalShadowFramePlan->m_Settings;
				const auto* shadows = blackboard.TryGet<RGShadowResources>(ShadowResourcesName);
				snapshot.m_Shadows = !context.m_DirectionalShadowFramePlan->m_ShadingEnabled
					? DisabledStatus() : shadows
					? CheckResourceActivity(graph, shadows->m_DirectionalShadowMap)
					: ViewRenderFeatureStatus{ ViewRenderFeatureState::Unavailable, ViewRenderFeatureReason::ResourcesUnavailable };
			}
			return snapshot;
		}

	private:
		static ViewRenderFeatureStatus DisabledStatus() noexcept
		{
			return { ViewRenderFeatureState::Disabled, ViewRenderFeatureReason::NotRequested };
		}

		static ViewRenderFeatureStatus CheckResourceActivity(
			const RenderGraph& graph, RGResourceHandle handle) noexcept
		{
			switch (graph.GetCompiledResourceStatus(handle))
			{
			case RGCompiledResourceStatus::Unavailable:
				return { ViewRenderFeatureState::Inactive, ViewRenderFeatureReason::ResourcesUnavailable };
			case RGCompiledResourceStatus::Culled:
				return { ViewRenderFeatureState::Inactive, ViewRenderFeatureReason::RenderGraphCulled };
			case RGCompiledResourceStatus::Referenced:
				return { ViewRenderFeatureState::Active, ViewRenderFeatureReason::None };
			}
			GGLAB_UNREACHABLE("Unhandled compiled resource status.");
		}

		static ViewRenderFeatureStatus ResolveForwardLightingStatus(ForwardPlusFrameStatus status) noexcept
		{
			switch (status)
			{
			case ForwardPlusFrameStatus::Active:
				return { ViewRenderFeatureState::Active, ViewRenderFeatureReason::None };
			case ForwardPlusFrameStatus::NoOpaqueDraws:
				return { ViewRenderFeatureState::Inactive, ViewRenderFeatureReason::NoOpaqueDraws };
			}
			GGLAB_UNREACHABLE("Unhandled Forward+ frame status.");
		}

		static ViewRenderFeatureStatus ResolveGTAOStatus(GTAOFrameStatus status) noexcept
		{
			switch (status)
			{
			case GTAOFrameStatus::Disabled: return DisabledStatus();
			case GTAOFrameStatus::Active:
				return { ViewRenderFeatureState::Active, ViewRenderFeatureReason::None };
			case GTAOFrameStatus::CoreCapabilityUnavailable:
				return { ViewRenderFeatureState::Unavailable, ViewRenderFeatureReason::CoreCapabilityUnavailable };
			case GTAOFrameStatus::PipelineUnavailable:
				return { ViewRenderFeatureState::Unavailable, ViewRenderFeatureReason::PipelineUnavailable };
			case GTAOFrameStatus::NoOpaqueDraws:
				return { ViewRenderFeatureState::Inactive, ViewRenderFeatureReason::NoOpaqueDraws };
			}
			GGLAB_UNREACHABLE("Unhandled GTAO frame status.");
		}

		static ViewRenderFeatureStatus ResolveTemporalStatus(const ResolvedTemporalFramePlan& plan) noexcept
		{
			if (plan.m_Active)
			{
				return { ViewRenderFeatureState::Active, ViewRenderFeatureReason::None };
			}
			switch (plan.m_DisableReason)
			{
			case TemporalAADisableReason::NotRequested: return DisabledStatus();
			case TemporalAADisableReason::CoreCapabilityUnavailable:
				return { ViewRenderFeatureState::Unavailable, ViewRenderFeatureReason::CoreCapabilityUnavailable };
			case TemporalAADisableReason::DisplayViewIneligible:
				return { ViewRenderFeatureState::Inactive, ViewRenderFeatureReason::DisplayViewIneligible };
			case TemporalAADisableReason::DepthVelocityPathUnavailable:
				return { ViewRenderFeatureState::Unavailable, ViewRenderFeatureReason::DepthVelocityPathUnavailable };
			case TemporalAADisableReason::SceneExtensionUnsupported:
				return { ViewRenderFeatureState::Unavailable, ViewRenderFeatureReason::SceneExtensionUnsupported };
			case TemporalAADisableReason::None:
				return { ViewRenderFeatureState::Inactive, ViewRenderFeatureReason::ResourcesUnavailable };
			}
			GGLAB_UNREACHABLE("Unhandled temporal disable reason.");
		}
	};

	RenderingSettingsDiagnosticsSnapshot BuildRenderingSettingsDiagnosticsSnapshot(
		const DiagnosticsFrameContext& context, const TemporalHistorySummary* history) noexcept
	{
		return RenderingSettingsDiagnosticsSnapshotBuilder::Build(context, history);
	}
}
