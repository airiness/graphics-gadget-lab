#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ApplicationInput.h"
#include "Capture/CaptureBundle.h"
#include "Capture/CaptureEnvironment.h"
#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "Capture/CapturePublisher.h"
#include "Capture/CaptureWriter.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/NonFiniteWatchCoordinator.h"
#include "Capture/RequiredFrameState.h"
#include "CapturePublicationTestFileSystem.h"
#include "Demo/DemoManager.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabFoundation/Platform/Win/Win32ProcessUtils.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabFoundation/Task/TaskTypes.h"
#include "GGLabRuntime/Diagnostics/Snapshots/TransientResourcePoolSnapshot.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/IBLPreviewTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalHistoryTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RenderServicesView.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/Resource/RenderTextureIndex.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabRuntime/Scene/Components.h"
#include "GGLabTestCore/SelfTest.h"
#include "GpuFaultLifecycleSelfTests.h"
#include "Lab/LabParameter.h"
#include "RuntimeIntegrationTestCases.h"
#include "RuntimeIntegrationTestServices.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <latch>
#include <memory>
#include <optional>
#include <ranges>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gglab::runtime_test
{
	RHIFrameEndResult RunOutcomeHostFrame(SelfTestContext& checks, GGLabAppRuntime& runtime,
		LifetimeProbe& probe, HostFrameAction action)
	{
		auto& host = *runtime.GetRenderHost();
		auto& demo = *runtime.GetDemoManager()->GetActiveDemo();
		// Keep the graph alive until the RenderFrame's RAII abort has settled it.
		RenderGraph graph(host.CreateRenderGraphCreateInfo());
		auto frame = host.BeginFrame();
		checks.Check(frame.IsReady(), "The production Renderer begins the outcome test frame");
		if (!frame.IsReady())
		{
			return RHIFrameEndResult::Fatal();
		}
		const auto resolution = ResolveNativeViewResolution({ 32, 32 });
		auto settings = ResolveViewRenderSettings(demo.GetViewRenderProfile(), demo.GetCamera());
		settings.m_TemporalAA.m_Enabled = false;
		settings.m_Lighting.m_GTAO.m_Enabled = false;
		const auto compatibility = probe.m_ContentServices.m_RenderServices.m_Temporal->GetTemporalHistoryDiagnostics().m_Compatibility;
		const ResolvedTemporalFramePlan plan{ .m_DisplayViewId = RenderViewID::Main,
			.m_Services = TemporalService::ColorDepthHistory | TemporalService::FrameContinuity,
			.m_ResetIdentity = compatibility.m_ResetIdentity,
			.m_SessionIdentity = compatibility.m_SessionIdentity };
		auto& temporal = host.BeginTemporalFrame(
			frame, plan, resolution, settings.m_Exposure.m_PreExposure, std::nullopt);
		ShadowVisualizationSettings shadows;
		auto built = host.BuildFrame({ .m_World = demo.GetWorld(), .m_CameraRig = demo.GetCameraRig(),
			.m_ViewRenderProfile = demo.GetViewRenderProfile(), .m_ShadowVisualizationSettings = shadows,
			.m_DisplayViewSettings = settings, .m_TemporalFramePlan = plan,
			.m_TemporalFrameTransaction = temporal, .m_ViewResolution = resolution,
			.m_FrameSlotIndex = frame.GetFrameSlotIndex(), .m_BackBufferIndex = frame.GetBackBufferIndex(),
			.m_FrameSerial = frame.GetSerial() });
		auto context = built.MakeRenderFrameContext();
		demo.GetRenderPipeline().BuildRenderGraph(graph, context, probe.m_ContentServices.m_RenderServices);
		const bool compiled = graph.Compile();
		checks.Check(compiled, "The outcome test compiles the production content graph");
		if (!compiled || action == HostFrameAction::DiscardAfterBuild)
		{
			return RHIFrameEndResult::Fatal();
		}
		if (probe.m_ChangePreviewAfterBuild)
		{
			host.GetIBLPreviewControl()->SetIBLEnvironmentPreviewLayout(IBLPreviewLayout::Grid2x3);
			host.GetIBLPreviewControl()->SetIBLEnvironmentPreviewMip(1);
			host.GetIBLPreviewControl()->RequestIBLPreview(IBLPreviewType::Environment);
			host.GetPostProcessPreviewControl()->SetPostProcessPreviewExposureEV(2.0f);
			host.GetPostProcessPreviewControl()->RequestPostProcessPreview();
		}
		probe.m_RejectUploadWait = action == HostFrameAction::RejectUploadOrder;
		host.Render(frame, graph, context);
		if (action == HostFrameAction::DiscardAfterRecording)
		{
			return RHIFrameEndResult::Fatal();
		}
		if (probe.m_CommitEnqueueFault == CommitEnqueueFault::BeforeSubmission)
		{
			probe.m_RetainedExecuteContext->DeferCommit([&probe](const RGCommitContext&) noexcept
				{
					++probe.m_LateCommitsRun;
				});
		}
		return host.EndFrame(frame);
	}

	bool HasOutcomeTextureState(GGLabAppRuntime& runtime, TransientPoolSlotState state,
		const RHIFencePoint& fence)
	{
		TransientResourcePoolSnapshot snapshot;
		BuildTransientResourcePoolSnapshot(
			*runtime.GetRenderHost()->CreateRenderGraphCreateInfo().m_TransientResourcePool, snapshot);
		return std::ranges::any_of(snapshot.m_Textures, [&](const TransientTextureSlotSnapshot& slot)
			{
				return slot.m_LogicalName == "FaultLifecycle.Color" && slot.m_State == state &&
					slot.m_RetirementFence == fence;
			});
	}

	void RequestOutcomePreviews(GGLabAppRuntime& runtime, LifetimeProbe& probe)
	{
		probe.m_EnablePreviewCommits = true;
		runtime.GetRenderHost()->GetShadowPreviewControl()->RequestShadowPreview();
		auto* resources = probe.m_ContentServices.m_RenderServices.m_Resources;
		resources->EnsurePostProcessPreviewResources(32, 32);
		resources->RequestPostProcessPreview();
		resources->MarkIBLPreviewDirty(IBLPreviewType::Environment);
		resources->RequestIBLPreview(IBLPreviewType::Environment);
	}

	void CheckOutcomePreviews(SelfTestContext& checks, const LifetimeProbe& probe, bool submitted)
	{
		auto* resources = probe.m_ContentServices.m_RenderServices.m_Resources;
		checks.Check(resources->IsPostProcessPreviewRequested() != submitted &&
			resources->IsDirty(RenderTextureIndex::Preview_PostProcess) != submitted &&
			resources->HasPublishedPostProcessPreview() == submitted &&
			resources->ObserveShadowPreviewRequest().m_Requested &&
			resources->IsIBLPreviewRequested(IBLPreviewType::Environment) != submitted &&
			(submitted || resources->IsIBLPreviewDirty(IBLPreviewType::Environment)),
			"Production Renderer commits previews only on Submitted, preserves demand and dirty state otherwise, and keeps shadow demand without a producer");
	}

	void RunEndFrameOutcomeScenario(SelfTestContext& checks, const std::filesystem::path& root,
		FrameEndInjection injection, AbortInjection abort)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		RequestOutcomePreviews(runtime, probe);
		const auto previousFence = probe.m_ContentServices.m_RenderServices.m_Presentation->GetLastSubmittedFencePoint();
		const uint32_t callbacks = probe.m_SubmittedCallbacks;
		const uint32_t ends = probe.m_FrameEnds;
		const uint32_t aborts = probe.m_FrameAborts;
		const uint64_t capture = probe.m_CaptureControl->RequestCapture(FrameCaptureSource::Scene);
		probe.m_FrameEndInjection = injection;
		probe.m_AbortInjection = abort;
		const bool submitted = injection == FrameEndInjection::Completed;
		checks.Check(runtime.Tick() == (submitted ? AppRuntimeTickResult::Continue : AppRuntimeTickResult::Fatal) &&
			probe.m_FrameEnds == ends + 1 &&
			probe.m_FrameAborts == aborts + (injection == FrameEndInjection::RecordingFailure ? 1u : 0u) &&
			probe.m_SubmittedCallbacks == callbacks + (submitted ? 1u : 0u),
			"The production host accepts only Completed and runs no submission hooks after any EndFrame Fatal");
		const RHIFencePoint frameFence = injection == FrameEndInjection::RecordingFailure ?
			probe.m_LastAbortFence : probe.m_LastEndFence;
		const auto expectedFence = frameFence.IsValid() ? frameFence : previousFence;
		checks.Check(probe.m_ContentServices.m_RenderServices.m_Presentation->GetLastSubmittedFencePoint() == expectedFence &&
			HasOutcomeTextureState(runtime, TransientPoolSlotState::PendingRetirement, expectedFence),
			"Submitted and Failed retire the recorded graph on the returned fence or the previous retention fence");
		std::vector<FrameCaptureResult> results;
		probe.m_CaptureControl->ConsumeResults(results);
		checks.Check(capture != 0 && results.size() == 1 && results[0].m_RequestId == capture &&
			results[0].m_Status == (submitted ? FrameCaptureStatus::Completed : FrameCaptureStatus::Failed) &&
			bool(results[0].m_Image) == submitted,
			"EndFrame Fatal fails the capture once, including fenceless and RHI-internal abort failures");
		const auto temporal = probe.m_ContentServices.m_RenderServices.m_Temporal->GetTemporalHistoryDiagnostics();
		checks.Check(submitted ?
			temporal.m_HistoryValid && temporal.m_LastCommitted.m_GraphicsFence == frameFence :
			!temporal.m_HasActiveHistory &&
				temporal.m_LastResetReason == TemporalHistoryResetReason::FatalSubmission,
			"Submitted commits temporal history on its fence; every EndFrame failure invalidates it");
		checks.Check(probe.m_CommitsRun == (submitted ? 2u : 0u) &&
			probe.m_CommitPayloadsDestroyed == 2 && probe.m_CommitIdentityMatches &&
			(!submitted || (probe.m_CommitObservations[0].m_Order == 1 &&
				probe.m_CommitObservations[1].m_Order == 2 &&
				probe.m_CommitObservations[1].m_Fence == frameFence)),
			"Production settlement commits once in order with its fence only on Submitted and drops Failed captures");
		CheckOutcomePreviews(checks, probe, submitted);
		if (!submitted)
		{
			const uint32_t begins = probe.m_FrameBegins;
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal && probe.m_FrameBegins == begins,
				"The failed production host begins no follow-up frame");
		}
		runtime.Shutdown();
		checks.Check(probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0,
			"Known-completion outcome teardown finalizes the backend once without early release");
		checks.Check(probe.m_CommitsRun == (submitted ? 2u : 0u) && probe.m_CommitPayloadsDestroyed == 2,
			"No ended frame's CPU commit survives into teardown");
	}

	void RunDiscardOutcomeScenario(SelfTestContext& checks, const std::filesystem::path& root,
		HostFrameAction action, AbortInjection abort)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		if (action == HostFrameAction::RejectUploadOrder)
		{
			// Force a real scene-table transfer so Renderer::Render must order after it.
			auto& registry = runtime.GetDemoManager()->GetActiveDemo()->GetWorld().GetRegistry();
			const auto light = registry.create();
			registry.emplace<components::TransformComponent>(light);
			registry.emplace<components::LightComponent>(light);
		}
		RequestOutcomePreviews(runtime, probe);
		const auto previousFence = probe.m_ContentServices.m_RenderServices.m_Presentation->GetLastSubmittedFencePoint();
		const uint32_t ends = probe.m_FrameEnds;
		const uint32_t aborts = probe.m_FrameAborts;
		const uint32_t executions = probe.m_GraphExecutions;
		const auto previousTemporal = probe.m_ContentServices.m_RenderServices.m_Temporal->GetTemporalHistoryDiagnostics();
		const uint64_t capture = probe.m_CaptureControl->RequestCapture(FrameCaptureSource::Scene);
		probe.m_AbortInjection = abort;
		GGLAB_UNUSED(RunOutcomeHostFrame(checks, runtime, probe, action));
		probe.m_RejectUploadWait = false;
		const bool recorded = action == HostFrameAction::DiscardAfterRecording;
		checks.Check(probe.m_FrameEnds == ends && probe.m_FrameAborts == aborts + 1 &&
			probe.m_GraphExecutions == executions + (recorded ? 1u : 0u) &&
			(action != HostFrameAction::RejectUploadOrder || probe.m_RejectedUploadWaits >= 1),
			"Build deferral, upload-order failure and recorded RAII abort each settle through RHI AbortFrame once");
		const bool unknown = abort == AbortInjection::CompletionUnknown;
		const auto expectedFence = probe.m_LastAbortFence.IsValid() ? probe.m_LastAbortFence : previousFence;
		checks.Check(probe.m_ContentServices.m_RenderServices.m_Presentation->GetLastSubmittedFencePoint() == expectedFence &&
			(unknown ? !recorded || HasOutcomeTextureState(runtime, TransientPoolSlotState::Leased, {}) :
				!recorded || HasOutcomeTextureState(runtime, TransientPoolSlotState::PendingRetirement, expectedFence)),
			"Renderer abort preserves the previous fence, adopts a retirement fence, or retains unknown work");
		std::vector<FrameCaptureResult> results;
		probe.m_CaptureControl->ConsumeResults(results);
		checks.Check(probe.m_CommitsRun == 0 && probe.m_CommitPayloadsDestroyed == (recorded ? 2u : 0u),
			"Discarded and unknown-completion frames destroy recorded CPU commits without running them");
		CheckOutcomePreviews(checks, probe, false);
		if (unknown)
		{
			checks.Check(results.size() == 1 && results[0].m_RequestId == capture &&
				results[0].m_Status == FrameCaptureStatus::Failed && !results[0].m_Image &&
				probe.m_ReadbackMaps == 0 && probe.m_EarlyDestructions == 0 &&
				runtime.GetRenderHost()->BeginFrame().IsFatal(),
				"AbortFrame CompletionUnknown fails capture unread and poisons the real Renderer");
		}
		else
		{
			const auto temporal = probe.m_ContentServices.m_RenderServices.m_Temporal->GetTemporalHistoryDiagnostics();
			checks.Check(results.empty() && probe.m_CaptureControl->GetUnfinishedRequestCount() == 1 &&
				temporal.m_ResetCount == previousTemporal.m_ResetCount && temporal.m_HistoryValid &&
				temporal.m_ReadIndex == previousTemporal.m_ReadIndex &&
				temporal.m_LastCommitted.m_GraphicsFence == previousTemporal.m_LastCommitted.m_GraphicsFence,
				"Discarded requeues flexible captures and preserves the previously committed temporal history");
			probe.m_AbortInjection = AbortInjection::Unfenced;
			const auto next = RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit);
			CheckOutcomePreviews(checks, probe, true);
			probe.m_CaptureControl->ConsumeResults(results);
			checks.Check(next.IsCompleted() && results.size() == 1 && results[0].m_RequestId == capture &&
				results[0].m_Status == FrameCaptureStatus::Completed && results[0].m_Image,
				"The next submitted frame fulfills the original requeued request exactly once");
			checks.Check(probe.m_CommitsRun == 2 && probe.m_CommitIdentityMatches &&
				probe.m_CommitPayloadsDestroyed == (recorded ? 4u : 2u) &&
				probe.m_CommitObservations[0].m_Order == 1 && probe.m_CommitObservations[1].m_Order == 2 &&
				probe.m_CommitObservations[1].m_Fence == next.GetSubmittedFence(),
				"Only the follow-up frame's commits run; discarded callbacks do not leak into its submission");
		}
		runtime.Shutdown();
		checks.Check(probe.m_ContextDestructions == 1 && probe.m_SafeToRelease &&
			probe.m_EarlyDestructions == 0 && probe.m_LateSubmits == 0,
			"Abort outcome teardown verifies quiescence before releasing retained GPU owners");
	}

	void RunExportDeferralOutcomeScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		// Retain completed CPU payloads to fill the real capture service's shared budget.
		std::vector<FrameCaptureResult> held;
		for (uint32_t index = 0; index < 8; ++index)
		{
			GGLAB_UNUSED(probe.m_CaptureControl->RequestCapture(FrameCaptureSource::Scene));
			GGLAB_UNUSED(RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit));
			probe.m_CaptureControl->ConsumeResults(held);
		}
		checks.Check(held.size() == 8 && probe.m_CaptureControl->GetResourceExportFrameAvailability() ==
			FrameCaptureAvailability::Busy, "Eight held production payloads create genuine export backpressure");
		const uint32_t builds = probe.m_GraphBuilds;
		const uint32_t executions = probe.m_GraphExecutions;
		const uint32_t ends = probe.m_FrameEnds;
		const uint32_t aborts = probe.m_FrameAborts;
		const uint32_t callbacks = probe.m_SubmittedCallbacks;
		const uint32_t commits = probe.m_CommitsRun;
		RequestOutcomePreviews(runtime, probe);
		probe.m_QueueExport = true;
		checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue && probe.m_ExportRequest != 0 &&
			probe.m_GraphBuilds == builds + 1 && probe.m_GraphExecutions == executions &&
			probe.m_FrameEnds == ends && probe.m_FrameAborts == aborts + 1 &&
			probe.m_SubmittedCallbacks == callbacks,
			"Production AppRuntime export deferral aborts after graph build without recording or submitting");
		checks.Check(probe.m_CommitsRun == commits && probe.m_CommitPayloadsDestroyed == commits,
			"The export-deferral abort carries no CPU commits from graph setup");
		CheckOutcomePreviews(checks, probe, false);
		held.clear();
		checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue && probe.m_FrameEnds == ends + 1 &&
			probe.m_SubmittedCallbacks == callbacks + 1,
			"Production AppRuntime continues with one successful frame after export pressure clears");
		CheckOutcomePreviews(checks, probe, true);
		std::vector<FrameCaptureResult> results;
		probe.m_CaptureControl->ConsumeResults(results);
		checks.Check(results.size() == 1 && results[0].m_RequestId == probe.m_ExportRequest &&
			results[0].m_Status == FrameCaptureStatus::Completed && results[0].m_Resources,
			"The deferred resource request is fulfilled by the follow-up frame exactly once");
		checks.Check(probe.m_CommitsRun == commits + 2 && probe.m_CommitPayloadsDestroyed == commits + 2 &&
			probe.m_CommitIdentityMatches,
			"The post-deferral submission commits only its own state once");
		runtime.Shutdown();
	}

	void RunPreviewChangeScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		RequestOutcomePreviews(runtime, probe);
		const auto before = runtime.GetRenderHost()->GetIBLPreviewView()->GetIBLPreviewResourcesDiagnostics().m_EnvironmentPreview;
		probe.m_ChangePreviewAfterBuild = true;
		const auto first = RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit);
		const auto stale = runtime.GetRenderHost()->GetIBLPreviewView()->GetIBLPreviewResourcesDiagnostics().m_EnvironmentPreview;
		auto* resources = probe.m_ContentServices.m_RenderServices.m_Resources;
		checks.Check(first.IsCompleted() && stale.m_Dirty && stale.m_Requested &&
			stale.m_PublishedLayout == before.m_Layout && stale.m_PublishedMip == before.m_SelectedMip &&
			stale.m_UpdateCount == before.m_UpdateCount + 1 &&
			resources->IsPostProcessPreviewRequested() && resources->IsDirty(RenderTextureIndex::Preview_PostProcess),
			"Build-to-execute selection changes preserve dirty state and newer requests while publishing only the drawn IBL selection");
		probe.m_ChangePreviewAfterBuild = false;
		const auto next = RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit);
		const auto current = runtime.GetRenderHost()->GetIBLPreviewView()->GetIBLPreviewResourcesDiagnostics().m_EnvironmentPreview;
		checks.Check(next.IsCompleted() && !current.m_Requested &&
			current.m_PublishedLayout == static_cast<uint32_t>(IBLPreviewLayout::Grid2x3) &&
			current.m_PublishedMip == 1 && current.m_UpdateCount == stale.m_UpdateCount + 1,
			"The next requesting frame publishes the newer IBL layout and mip exactly once");
		CheckOutcomePreviews(checks, probe, true);
		runtime.Shutdown();
	}

	void RunLateCommitScenario(SelfTestContext& checks, const std::filesystem::path& root,
		CommitEnqueueFault fault, AbortInjection abort, bool throughRuntime)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		const auto previousFence = probe.m_Presentation->GetLastSubmittedFencePoint();
		const auto previousTemporal = probe.m_ContentServices.m_RenderServices.m_Temporal->GetTemporalHistoryDiagnostics();
		const uint32_t ends = probe.m_FrameEnds;
		const uint32_t aborts = probe.m_FrameAborts;
		const uint32_t callbacks = probe.m_SubmittedCallbacks;
		const uint64_t capture = probe.m_CaptureControl->RequestCapture(FrameCaptureSource::Scene);
		probe.m_CommitEnqueueFault = fault;
		probe.m_AbortInjection = abort;
		const bool submitted = fault != CommitEnqueueFault::BeforeSubmission;
		// A list that failed before the commits start discards them; once they run, the
		// original batch completes.
		const bool committed = fault == CommitEnqueueFault::DuringSettlement;
		if (submitted && throughRuntime)
		{
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal,
				"Settlement-time late enqueue makes the production AppRuntime stop with Fatal");
		}
		else
		{
			const auto result = RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit);
			checks.Check(!result.IsCompleted() && result.GetSubmittedFence() ==
				(submitted ? probe.m_LastEndFence : probe.m_LastAbortFence),
				"Late enqueue returns Fatal with the real submission or abort fence");
		}
		checks.Check(probe.m_FrameEnds == ends + (submitted ? 1u : 0u) &&
			probe.m_FrameAborts == aborts + (submitted ? 0u : 1u) &&
			probe.m_SubmittedCallbacks == callbacks && probe.m_LateCommitsRun == 0 &&
			probe.m_CommitsRun == (committed ? 2u : 0u) && probe.m_CommitPayloadsDestroyed == 2 &&
			probe.m_CommitIdentityMatches,
			"Late enqueue never runs its callback, never resettles owners, and releases every CPU capture once");
		const auto expectedFence = submitted ? probe.m_LastEndFence :
			(probe.m_LastAbortFence.IsValid() ? probe.m_LastAbortFence : previousFence);
		checks.Check(probe.m_Presentation->GetLastSubmittedFencePoint() == expectedFence &&
			HasOutcomeTextureState(runtime, TransientPoolSlotState::PendingRetirement, expectedFence),
			"Late enqueue preserves the actual submission or abort retirement fence");
		const auto temporal = probe.m_ContentServices.m_RenderServices.m_Temporal->GetTemporalHistoryDiagnostics();
		checks.Check(temporal.m_HistoryValid && (submitted ?
			temporal.m_LastCommitted.m_GraphicsFence == expectedFence && (!committed ||
				probe.m_CommitObservations[0].m_Order == 1 && probe.m_CommitObservations[1].m_Order == 2 &&
				probe.m_CommitObservations[1].m_Fence == expectedFence) :
			temporal.m_LastCommitted.m_GraphicsFence == previousTemporal.m_LastCommitted.m_GraphicsFence &&
				temporal.m_ResetCount == previousTemporal.m_ResetCount),
			"A settlement violation keeps Submitted history; a sealed-list abort preserves previous history");
		std::vector<FrameCaptureResult> results;
		probe.m_CaptureControl->ConsumeResults(results);
		checks.Check(submitted ? results.size() == 1 && results[0].m_RequestId == capture &&
			results[0].m_Status == FrameCaptureStatus::Completed && results[0].m_Image :
			results.empty() && probe.m_CaptureControl->GetUnfinishedRequestCount() == 1,
			"Late enqueue does not change the capture's original Submitted or Discarded outcome");
		if (submitted && throughRuntime)
		{
			const uint32_t begins = probe.m_FrameBegins;
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal && probe.m_FrameBegins == begins,
				"AppRuntime begins no follow-up frame after settlement returns Fatal");
		}
		probe.m_RetainedExecuteContext.reset();
		runtime.Shutdown();
		checks.Check(probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0 &&
			probe.m_CommitsRun == (committed ? 2u : 0u) && probe.m_CommitPayloadsDestroyed == 2,
			"Late-enqueue teardown neither repeats commits nor releases GPU resources early");
	}

	void RunForwardPlusValidationScenario(SelfTestContext& checks, const std::filesystem::path& root,
		RHIFenceQueryResult completion, bool resize, bool nativeQuery)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		probe.m_EnableForwardPlusReadback = true;
		checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue &&
			probe.m_ForwardPlusFence.IsValid() && probe.m_ForwardPlusReadback->GetScheduledCount() == 1,
			"The production Renderer adopts a Forward+ readback fence only after Submitted");
		const auto fence = probe.m_ForwardPlusFence;
		const auto begins = probe.m_FrameBegins;
		const auto ends = probe.m_FrameEnds;
		const auto aborts = probe.m_FrameAborts;
		const auto builds = probe.m_GraphBuilds;
		const auto executions = probe.m_GraphExecutions;
		const auto submits = probe.m_FrameSubmits;
		const auto maps = probe.m_ReadbackMaps;
		const auto preparations = probe.m_ValidationPreparations;
		probe.m_SafeToRelease = false;
		probe.m_ForwardPlusQuery = completion;
		probe.m_ObserveForwardPlusQuery = nativeQuery;
		probe.m_ResizeValidation = resize;
		if (completion == RHIFenceQueryResult::DeviceLost)
		{
			probe.m_Outcome = TeardownOutcome::DeviceLost;
		}
		checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal &&
			probe.m_FrameBegins == begins + 1 && probe.m_FrameEnds == ends &&
			probe.m_FrameAborts == aborts + 1 && probe.m_GraphBuilds == builds &&
			probe.m_GraphExecutions == executions && probe.m_FrameSubmits == submits &&
			probe.m_ReadbackMaps == maps && probe.m_ValidationPreparations == preparations &&
			probe.m_ForwardPlusReadback->GetScheduledCount() == 1,
			"Readback validation fatally aborts the production host before preparation, graph build/compile/execute or resubmission");
		checks.Check(probe.m_LastValidation.m_Status == (completion == RHIFenceQueryResult::DeviceLost ?
			RenderFrameValidationStatus::DeviceLost : RenderFrameValidationStatus::ContractFailure) &&
			probe.m_LastValidation.m_Detail.find("value=" + std::to_string(fence.m_Value)) != std::string::npos &&
			probe.m_LastValidation.m_Detail.find(completion == RHIFenceQueryResult::DeviceLost ? "DeviceLost" :
				completion == RHIFenceQueryResult::Pending ? "Pending" :
				nativeQuery ? "native completion failure" : "rejected query") != std::string::npos,
			"The host receives the actual Forward+ classifier verdict and original query evidence even during resize");
		checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal && probe.m_FrameBegins == begins + 1,
			"Fatal Forward+ validation prevents the next frame from beginning in Debug and Release");
		runtime.Shutdown();
		checks.Check(probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0 &&
			probe.m_LateSubmits == 0 && probe.m_ReadbackMaps == maps,
			"Checked idle or confirmed-device-loss teardown releases retained resources without mapping or late submissions");
	}

	void RunPublicationRetentionScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool claimed)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		auto* capture = probe.m_CaptureControl;
		GGLAB_UNUSED(capture->RequestCapture(FrameCaptureSource::Scene));
		GGLAB_UNUSED(capture->RequestCapture(FrameCaptureSource::Scene));
		GGLAB_UNUSED(RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit));
		std::vector<FrameCaptureResult> products;
		capture->ConsumeResults(products);
		const bool shared = products.size() == 2 && products[0].m_Image &&
			products[0].m_Image == products[1].m_Image;
		checks.Check(shared, "Two production capture subscribers share one fenced immutable CPU image");
		if (!shared)
		{
			runtime.Shutdown();
			return;
		}
		std::weak_ptr<const FrameCaptureImage> image = products[0].m_Image;
		auto files = std::make_shared<CapturePublicationTestFileSystem>();
		files->m_Block = claimed ?
			CapturePublicationTestFileSystem::Block::Publication :
			CapturePublicationTestFileSystem::Block::TemporaryWrite;
		auto writer = std::make_unique<CaptureWriter>(
			[](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
			{
				return std::vector<uint8_t>{ 1, 2, 3 };
			}, false, files);
		std::array<std::shared_ptr<CaptureWriteJob>, 2> jobs;
		for (size_t i = 0; i < jobs.size(); ++i)
		{
			jobs[i] = std::make_shared<CaptureWriteJob>();
			const std::string runIdentity = CreateCaptureRunIdentity();
			jobs[i]->m_Bundle = { .m_RunIdentity = runIdentity,
				.m_Environment = SerializeCaptureEnvironment({}, runIdentity),
				.m_JobId = i + 1, .m_Attempt = 1, .m_ProductCount = 1,
				.m_Deliveries = { { .m_Input = { .m_Image = products[i].m_Image,
					.m_Submission = { products[i].m_SubmissionFenceIndex, products[i].m_SubmissionFenceGeneration,
						products[i].m_SubmissionFenceValue, products[i].m_AdapterCompatibilityIdentity },
					.m_Metadata = { .m_RequestId = i + 1, .m_Backend = "dx12",
						.m_FrameSerial = products[i].m_FrameSerial, .m_Width = 32, .m_Height = 32 } } } } };
			jobs[i]->m_Directory = root / "Published";
			jobs[i]->m_BaseStem = i == 0 ? "running" : "queued";
			checks.Check(writer->Submit(jobs[i]), "The production product enters the independent writer");
		}
		products.clear();
		checks.Check(files->WaitEntered(), "Production capture transport reaches the blocked publication boundary");
		// Seven other physical products saturate the real eight-slot budget.
		// The two writer subscribers must retain exactly one additional credit.
		for (uint32_t i = 0; i < 7; ++i)
		{
			GGLAB_UNUSED(capture->RequestCapture(FrameCaptureSource::Scene));
			GGLAB_UNUSED(RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit));
			capture->ConsumeResults(products);
		}
		checks.Check(products.size() == 7 && capture->GetAvailability(RHIFormat::R8G8B8A8Unorm, 32, 32) ==
			FrameCaptureAvailability::Busy,
			"Blocked shared writer products conserve real Runtime payload credit after GPU buffers retire");
		const auto running = writer->Abandon(*jobs[0], std::chrono::milliseconds(0));
		const auto queued = writer->Abandon(*jobs[1], std::chrono::milliseconds(0));
		checks.Check(running.m_Publication == (claimed ?
			CapturePublicationOutcome::Indeterminate :
			CapturePublicationOutcome::NotPublished) &&
			queued.m_Publication == CapturePublicationOutcome::NotPublished &&
			capture->GetAvailability(RHIFormat::R8G8B8A8Unorm, 32, 32) == FrameCaptureAvailability::Busy,
			"Timeout receipts do not release credits still held by running or queued consumers");
		runtime.Shutdown();
		const auto maps = probe.m_ReadbackMaps;
		products.clear();
		writer.reset();
		checks.Check(probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0 && !image.expired(),
			"The real Renderer and device can retire while an independent writer retains CPU products");
		files->Release();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		for (const auto& job : jobs)
		{
			job->WaitUntil(deadline);
		}
		const auto late = jobs[0]->GetReceipt();
		const auto skipped = jobs[1]->GetReceipt();
		checks.Check(late && skipped &&
			late->m_Publication == (claimed ?
				CapturePublicationOutcome::Published :
				CapturePublicationOutcome::NotPublished) &&
			skipped->m_Publication == CapturePublicationOutcome::NotPublished && image.expired() &&
			probe.m_ReadbackMaps == maps &&
			std::filesystem::exists(root / "Published/running.capture-bundle.json") == claimed &&
			!std::filesystem::exists(root / "Published/queued.capture-bundle.json"),
			"Late worker completion releases the last CPU lease without touching a destroyed device or publishing abandoned work");
	}

	void RunServicePublicationScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		RenderServicesView retained;
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			checks.Check(!runtime.GetRenderServices().IsValid(),
				"Uninitialized AppRuntime publishes no render services");
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root / "shutdown"))
			{
				return;
			}
			retained = runtime.GetRenderServices();
			probe.m_SavedServiceView = retained;
			probe.m_CheckServiceCleanup = true;
			bool frameInjectionsAbsent = false;
			checks.Check(retained.TryUse([&](const RenderServices& services) noexcept
				{
					frameInjectionsAbsent = !services.m_TextureAssets && !services.m_OverlayExtension;
				}) && frameInjectionsAbsent,
				"Running AppRuntime publishes stable services without per-frame injections");
			runtime.HandleHostEvent({ .m_Type = AppHostEventType::Suspended });
			checks.Check(retained.IsValid() && runtime.GetRenderServices().IsValid(),
				"Suspension preserves published services");
			runtime.HandleHostEvent({ .m_Type = AppHostEventType::Resumed });
			std::latch registered(1);
			std::latch cancelled(1);
			bool cancellationRejected = false;
			const auto task = runtime.GetTaskSystem()->Submit({}, [&](std::stop_token token) noexcept
				{
					std::stop_callback callback(token, [&]() noexcept
						{
							cancellationRejected = !runtime.GetRenderServices().IsValid() &&
								!retained.IsValid() && runtime.GetLifecycleState() ==
								AppRuntimeLifecycleState::ShuttingDown;
							cancelled.count_down();
						});
					registered.count_down();
					// Keep the registration alive until its callback runs. The stop
					// bit can become visible before request_stop invokes the callback.
					cancelled.wait();
					return TaskResult::Success();
				});
			checks.Check(task.IsValid(), "Service shutdown regression registers a cancellable task");
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (task.IsValid() && !registered.try_wait() &&
				std::chrono::steady_clock::now() < deadline)
			{
				std::this_thread::yield();
			}
			if (!registered.try_wait())
			{
				checks.Check(false, "The service cancellation fixture starts within its deadline");
				runtime.Shutdown();
				return;
			}
			bool toolingRejected = false;
			ParameterTestTooling tooling;
			tooling.m_OnShutdown = [&]() noexcept
				{
					toolingRejected = !runtime.GetRenderServices().IsValid() && !retained.IsValid() &&
						runtime.GetRenderHost()->IsInitialized();
				};
			runtime.Shutdown({ .m_ApplicationTooling = &tooling });
			checks.Check(cancellationRejected, "Shutdown revokes before task cancellation callbacks");
			checks.Check(toolingRejected, "Shutdown revokes before tooling callbacks");
			checks.Check(probe.m_ContainedCleanupAfterRevocation,
				"Contained cleanup retains live providers after revocation");
			bool invoked = false;
			checks.Check(!runtime.GetRenderServices().IsValid() && !retained.TryUse(
				[&](const RenderServices&) noexcept { invoked = true; }) && !invoked,
				"Stopped AppRuntime rejects both new and previously retained service views");
			std::string error;
			checks.Check(!runtime.GetFrameCaptureCoordinator()->HasNonFiniteInstrumentation() &&
				!runtime.GetNonFiniteWatchCoordinator()->Configure({}, error),
				"Post-shutdown capture/watch queries cannot reach destroyed render services");
			runtime.GetNonFiniteWatchCoordinator()->PrepareFrame(99999);
			GGLAB_UNUSED(runtime.GetParameterControl()->GetParameterBatchStatuses({}));
			runtime.Shutdown();
		}
		checks.Check(!retained.IsValid(), "A saved view safely outlives AppRuntime destruction");
		for (const auto terminal : { "exit", "failure", "rollback" })
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root / terminal))
			{
				continue;
			}
			const auto saved = runtime.GetRenderServices();
			if (std::string_view(terminal) == "exit")
			{
				runtime.HandleHostEvent({ .m_Type = AppHostEventType::ExitRequested });
			}
			else if (std::string_view(terminal) == "failure")
			{
				probe.m_FrameEndInjection = FrameEndInjection::FatalFenced;
				GGLAB_UNUSED(runtime.Tick());
			}
			else
			{
				checks.Check(AppRuntimeServicePublicationTestAccess::RollBackPartialInitialization(runtime),
					"Lifecycle-only Running state does not publish a partially initialized service bundle");
			}
			checks.Check(!saved.IsValid() && !runtime.GetRenderServices().IsValid(),
				"Exit, failure and partial-initialization rollback revoke saved service views");
			runtime.Shutdown();
		}
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			const auto artifactRoot = root / "direct-finalize";
			TaskSystem tasks;
			ShaderManager manager({ .m_ActiveBackend = RHIBackendType::DX12,
				.m_ArtifactRoot = artifactRoot, .m_ActiveRegistry = WriteShaderFixtures(artifactRoot) });
			auto instance = CreateRenderHost({ .m_RHIContextFactory = &factory,
				.m_ShaderManager = &manager, .m_TaskSystem = &tasks,
				.m_IblDerivedDataCacheDirectory = root / "direct-ibl",
				.m_Width = 32, .m_Height = 32 });
			checks.Check(instance.m_Host && instance.m_ServiceView.IsValid() && !instance.m_Diagnostics,
				"The production host factory publishes services without opting into diagnostics");
			if (instance.m_Host)
			{
				instance.m_Host->Finalize();
				checks.Check(!instance.m_ServiceView.IsValid() && !instance.m_Host->IsInitialized(),
					"Direct Finalize revokes views while the host object remains allocated");
			}
			tasks.Shutdown();
			tasks.PumpCompletions();
		}
		for (const auto scenario : { "shutdown", "tick", "finalize" })
		{
			const auto child = RunSelfTestChild("--service-publication-child", scenario);
			checks.Check(child.m_Completed && child.m_ExitCode != 0 && child.m_Output.find(
				"lifecycle operation during a service borrow") != std::string::npos,
				"Production AppRuntime and RenderHost reject lifecycle reentry before releasing services");
		}
	}

	void RunControlDeadlineScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		runtime.EnableParameterControl();
		const auto stamp = runtime.GetHostViewControlStamp();
		auto required = std::make_shared<RequiredFrameState>();
		required->m_RunIdentity = stamp.m_RunIdentity;
		required->m_ContentIdentity = stamp.m_ContentIdentity;
		required->m_MinHostViewRevision = stamp.m_HostViewRevision;
		required->m_Values = { { LabParameterId("view.taa.enabled"), LabParameterType::Bool, false } };
		runtime.HandleHostEvent({ .m_Type = AppHostEventType::Suspended });
		const auto admission = runtime.TrySubmitCaptureJob({ .m_RequiredProducts = { ImageCaptureProduct{} },
			.m_Timing = FrameCaptureTiming::AfterReady, .m_SettleFrames = 1000,
			.m_RequiredFrameState = std::move(required), .m_FrameSelectionTimeout = std::chrono::seconds(1) });
		const auto deadline = runtime.GetNextControlDeadline();
		checks.Check(admission.m_Status == CaptureAdmissionStatus::Accepted && deadline.has_value(),
			"Suspended AppRuntime admits a predicate job and exposes its monotonic control deadline");
		if (deadline)
		{
			const auto executions = probe.m_GraphExecutions;
			const auto serial = runtime.GetFrameCaptureCoordinator()->GetLastFrameState()->m_FrameSerial;
			runtime.ServiceControlDeadlines(*deadline);
			const auto* result = runtime.FindCaptureJobResult(admission.m_JobId);
			checks.Check(result && result->m_FailureCode == "FrameSelectionExpired" &&
				result->m_Publication == CaptureJobPublication::NotPublished &&
				runtime.GetCaptureIntakeSnapshot().m_ActiveJobs == 0 &&
				runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 1 &&
				probe.m_GraphExecutions == executions &&
				runtime.GetFrameCaptureCoordinator()->GetLastFrameState()->m_FrameSerial == serial &&
				runtime.GetLifecycleState() == AppRuntimeLifecycleState::Suspended,
				"Frame-free deadline servicing publishes the real failure and releases active capacity without GPU work");
			GGLAB_UNUSED(runtime.TakeCaptureJobResult(admission.m_JobId));
			runtime.HandleHostEvent({ .m_Type = AppHostEventType::Resumed });
			GGLAB_UNUSED(runtime.Tick());
			checks.Check(!runtime.FindCaptureJobResult(admission.m_JobId) &&
				!runtime.GetNextControlDeadline() && probe.m_GraphExecutions == executions + 1,
				"Resume renders normally without reviving an expired capture or its consumed receipt");
		}
		runtime.Shutdown();
	}

	void RunFrameOutcomeScenarios(SelfTestContext& checks, const std::filesystem::path& root)
	{
		for (const auto injection : { FrameEndInjection::Completed, FrameEndInjection::FatalFenced,
			FrameEndInjection::FatalUnfenced, FrameEndInjection::RecordingFailure })
		{
			RunEndFrameOutcomeScenario(checks, root / ("end-" + std::to_string(static_cast<int>(injection))),
				injection, AbortInjection::Unfenced);
		}
		RunEndFrameOutcomeScenario(checks, root / "internal-abort-fenced",
			FrameEndInjection::RecordingFailure, AbortInjection::Fenced);
		for (const auto action : { HostFrameAction::DiscardAfterBuild,
			HostFrameAction::DiscardAfterRecording, HostFrameAction::RejectUploadOrder })
		{
			for (const auto abort : { AbortInjection::Unfenced, AbortInjection::Fenced })
			{
				const auto name = "abort-" + std::to_string(static_cast<int>(action)) +
					"-" + std::to_string(static_cast<int>(abort));
				RunDiscardOutcomeScenario(checks, root / name, action, abort);
			}
		}
		RunDiscardOutcomeScenario(checks, root / "abort-unknown", HostFrameAction::DiscardAfterRecording,
			AbortInjection::CompletionUnknown);
		RunExportDeferralOutcomeScenario(checks, root / "export-deferral");
		RunPreviewChangeScenario(checks, root / "preview-change");
		RunLateCommitScenario(checks, root / "late-sealed-unfenced",
			CommitEnqueueFault::BeforeSubmission, AbortInjection::Unfenced);
		RunLateCommitScenario(checks, root / "late-sealed-fenced",
			CommitEnqueueFault::BeforeSubmission, AbortInjection::Fenced);
		RunLateCommitScenario(checks, root / "late-submitted",
			CommitEnqueueFault::AfterSubmission, AbortInjection::Unfenced);
		RunLateCommitScenario(checks, root / "late-submitted-host-fence",
			CommitEnqueueFault::AfterSubmission, AbortInjection::Unfenced, false);
		RunLateCommitScenario(checks, root / "late-settling",
			CommitEnqueueFault::DuringSettlement, AbortInjection::Unfenced);
		RunLateCommitScenario(checks, root / "late-settling-host-fence",
			CommitEnqueueFault::DuringSettlement, AbortInjection::Unfenced, false);
	}
}

namespace gglab
{
	using namespace runtime_test;

	void RunFrameSettlementIntegrationSelfTests(SelfTestContext& context) noexcept
	{
		RuntimeTestDirectory directory("settlement");
		const auto& root = directory.GetPath();
		RunServicePublicationScenario(context, root / "service-publication");
		RunControlDeadlineScenario(context, root / "control-deadline");
		RunFrameOutcomeScenarios(context, root / "outcomes");
		RunPublicationRetentionScenario(context, root / "publication-before-claim", false);
		RunPublicationRetentionScenario(context, root / "publication-after-claim", true);
	}

	int RunServicePublicationChild(std::string_view scenario) noexcept
	{
		if (scenario != "shutdown" && scenario != "tick" && scenario != "finalize")
		{
			return 2;
		}
		ConsoleSelfTestReporter reporter;
		SelfTestContext checks(reporter);
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		const auto root = std::filesystem::temp_directory_path() /
			("gglab-service-child-" + std::to_string(GetCurrentProcessId()));
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return 90;
		}
		runtime.GetRenderServices().TryUse([&](const RenderServices&) noexcept
			{
				if (scenario == "shutdown")
				{
					runtime.Shutdown();
				}
				else if (scenario == "tick")
				{
					GGLAB_UNUSED(runtime.Tick());
				}
				else
				{
					runtime.GetRenderHost()->Finalize();
				}
			});
		return 91;
	}
}
