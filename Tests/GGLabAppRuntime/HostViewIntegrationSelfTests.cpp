#include "ApplicationInput.h"
#include "ApplicationToolingIntegration.h"
#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "Capture/RequiredFrameState.h"
#include "Demo/DemoManager.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineOverlayExtensionBase.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabTestCore/SelfTest.h"
#include "GpuFaultLifecycleSelfTests.h"
#include "Lab/LabParameter.h"
#include "Parameters/HostViewParameters.h"
#include "Parameters/ParameterBatch.h"
#include "Parameters/ParameterEffectReceipt.h"
#include "Parameters/ParameterRequest.h"
#include "Parameters/ViewControlSnapshot.h"
#include "RuntimeIntegrationTestCases.h"
#include "RuntimeIntegrationTestServices.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gglab::runtime_test
{
	void RunHostParameterScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool withLab)
	{
		LifetimeProbe probe;
		probe.m_EnableParameterLab = withLab;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		auto& parameters = *runtime.GetParameterControl();
		probe.m_ParameterControl = &parameters;
		const auto revisionBeforeDiscovery =
			runtime.GetFrameCaptureCoordinator()->GetLastFrameState()->m_ParameterRevision;
		checks.Check(revisionBeforeDiscovery != 0,
			"Production frames already carry a valid parameter identity before observer activation");
		runtime.EnableParameterControl();
		checks.Check(parameters.IsAvailable() && parameters.GetHostParameters().GetParameters().size() == 40 &&
			(parameters.GetLabParameters() != nullptr) == withLab,
			"Host descriptors are available with Demo content and alongside the active Lab source");
		std::string error;
		const auto initial = parameters.GetParameterRevision();
		const LabParameterId inheritedRadius("view.gtao.radius");
		const std::array inherited{ LabParameterValue{ inheritedRadius,
			parameters.GetHostParameters().Find(inheritedRadius)->m_Value } };
		const auto unchangedId = runtime.RequestParameters(AssignmentRequest(runtime, inherited), error);
		GGLAB_UNUSED(runtime.Tick());
		const auto unchanged = runtime.TakeParameterBatchResult(unchangedId);
		checks.Check(unchanged && unchanged->m_Items.front().m_State == ParameterEffectState::NoChange &&
			parameters.GetParameterRevision() == initial &&
			runtime.GetFrameCaptureCoordinator()->GetLastFrameState()->m_ParameterRevision == revisionBeforeDiscovery,
			"Initial discovery and a first NoChange preserve the frame identity used by capture settling");
		for (const auto& invalid : std::array{
			LabParameterValue{ LabParameterId("view.exposure.ev100"), 25.0f },
			LabParameterValue{ LabParameterId("view.gtao.direction_count"), int32_t(2) },
			LabParameterValue{ LabParameterId("view.taa.history_filter"), int32_t(99) },
			LabParameterValue{ LabParameterId("unknown.host"), true } })
		{
			std::vector<LabParameterValue> values{ { LabParameterId("view.gtao.radius"), 3.0f }, invalid };
			if (withLab)
			{
				values.push_back({ LabParameterId("radius"), 2.0f });
			}
			checks.Check(runtime.RequestParameters(AssignmentRequest(runtime, values), error) == 0 &&
				!parameters.GetParameterBatchStatus() && parameters.GetParameterRevision() == initial &&
				(!withLab || probe.m_ParameterRuntime->GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f) == 1.0f),
				"Invalid host/type/enum/range items reject every source without live mutation or receipt allocation");
		}
		std::vector<LabParameterValue> values
		{
			{ LabParameterId("view.gtao.radius"), 3.0f },
			{ LabParameterId("view.exposure.ev100"), 8.0f },
			{ LabParameterId("view.exposure.compensation"), 1.0f },
			{ LabParameterId("view.taa.max_history_feedback"), 0.7f },
			{ LabParameterId("view.taa.history_rectification"), int32_t(TemporalAAHistoryRectification::BoundedVarianceClip) },
		};
		if (withLab)
		{
			values.push_back({ LabParameterId("radius"), 2.0f });
		}
		const auto id = runtime.RequestParameters(AssignmentRequest(runtime, values), error);
		checks.Check(id != 0 && parameters.GetParameterBatchStatus()->m_Revision == 0 &&
			parameters.GetParameterBatchStatus()->m_Items.empty(),
			"Host/mixed admission owns values without predicting application stamps");
		probe.m_SkipParameterFrame = true;
		GGLAB_UNUSED(runtime.Tick());
		const auto* aborted = parameters.GetParameterBatchStatus();
		checks.Check(aborted && !aborted->IsTerminal() && aborted->m_Items.front().m_Published &&
			!aborted->m_Items.front().m_FirstConsumed && !runtime.TakeParameterBatchResult(id),
			"An aborted real host parameter frame retains application/publication but cannot be consumed");
		const auto revision = aborted->m_Revision;
		GGLAB_UNUSED(runtime.Tick());
		const auto* done = parameters.GetParameterBatchStatus();
		checks.Check(done && done->IsTerminal() && done->m_Revision == revision &&
			std::ranges::all_of(done->m_Items, [](const auto& item)
			{
				if (item.m_Parameter == LabParameterId("radius"))
				{
					return item.m_State == ParameterEffectState::Superseded && !item.m_FirstConsumed;
				}
				return item.m_State == ParameterEffectState::Consumed &&
					item.m_Identity.m_Revision != 0 && item.m_Published == item.m_Applied &&
					item.m_FirstConsumed > item.m_Published;
			}),
			"A successful retry consumes the frozen Host revision while its masked Lab value stays Superseded");
		const auto& view = probe.m_ParameterBuiltView;
		const auto expectedExposure = ResolveManualExposureSettings(8.0f, 1.0f,
			runtime.GetDemoManager()->GetActiveDemo()->GetViewRenderProfile().m_EnableScenePreExposure);
		checks.Check(view.m_Lighting.m_GTAO.m_Radius == 3.0f &&
			view.m_TemporalAA.m_MaxHistoryFeedback == 0.7f &&
			view.m_TemporalAA.m_HistoryRectification == TemporalAAHistoryRectification::BoundedVarianceClip &&
			view.m_Exposure.m_EffectiveEV100 == 7.0f &&
			view.m_Exposure.m_PreExposure == expectedExposure.m_PreExposure &&
			(!withLab || probe.m_ParameterRuntime->GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f) == 2.0f),
			"The production frame uses host overrides after Lab authoring, with coherent manual exposure and pre-exposure");
		checks.Check(runtime.RequestParameters(AssignmentRequest(runtime, values), error) == 0 &&
			!runtime.TakeParameterBatchResult(id + 1) && runtime.TakeParameterBatchResult(id).has_value(),
			"Every source shares one retained result slot and only the correct terminal id releases it");
		const auto nochange = runtime.RequestParameters(AssignmentRequest(runtime, values), error);
		GGLAB_UNUSED(runtime.Tick());
		const auto same = runtime.TakeParameterBatchResult(nochange);
		checks.Check(same && same->m_Revision == revision &&
			std::ranges::all_of(same->m_Items, [](const auto& item)
			{
				return item.m_Parameter == LabParameterId("radius") ?
					item.m_State == ParameterEffectState::Superseded && !item.m_FirstConsumed :
					item.m_State == ParameterEffectState::NoChange;
			}),
			"Repeating a mixed batch keeps masked Lab state Superseded and creates no effects");
		values.front().m_Value = 4.0f;
		const auto stale = runtime.RequestParameters(AssignmentRequest(runtime, values), error);
		if (withLab)
		{
			probe.m_ParameterRuntime->OnExit();
			probe.m_ParameterRuntime->RequestRestartSession();
			probe.m_ParameterRuntime->ProcessPendingCommands();
			probe.m_ParameterRuntime->OnEnter();
			GGLAB_UNUSED(runtime.Tick());
			const auto replaced = runtime.TakeParameterBatchResult(stale);
			checks.Check(replaced && replaced->m_State == ParameterBatchState::Failed &&
				replaced->m_Items.empty() && probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius != 4.0f,
				"Replacing the Lab session clears host overrides and never applies a stale cross-source batch");
		}
		else
		{
			probe.m_FrameEndInjection = FrameEndInjection::FatalFenced;
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal &&
				parameters.GetParameterBatchStatus()->m_State == ParameterBatchState::Failed &&
				!parameters.GetParameterBatchStatus()->m_Items.front().m_FirstConsumed,
				"Fatal host-only submission fails its effects without fabricated consumption");
		}
		runtime.Shutdown();
	}

	void RunEffectiveParameterSettingsScenario(SelfTestContext& checks,
		const std::filesystem::path& root, bool withLab)
	{
		LifetimeProbe probe;
		probe.m_EnableParameterLab = withLab;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		ParameterTestTooling tooling;
		tooling.m_Settings.m_GTAO = GTAOSettings{ .m_Radius = 2.0f };
		tooling.m_Settings.m_Bloom = BloomSettings{ .m_Intensity = 0.35f };
		tooling.m_Settings.m_ScenePreExposure = false;
		const auto tick = [&]()
			{
				return runtime.Tick({ .m_ApplicationTooling = &tooling });
			};
		GGLAB_UNUSED(tick());
		runtime.EnableParameterControl();
		const auto& control = *runtime.GetParameterControl();
		const LabParameterId hostRadius("view.gtao.radius");
		const LabParameterId labRadius("radius");
		std::string error;
		const auto requestHost = [&](float radius)
			{
				const std::array values{ LabParameterValue{ hostRadius, radius } };
				return runtime.RequestParameters(AssignmentRequest(runtime, values), error);
			};
		checks.Check(control.GetHostParameters().Get(hostRadius, 0.0f) == 2.0f &&
			probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius == 2.0f &&
			runtime.GetDemoManager()->GetActiveDemo()->GetViewRenderProfile().m_Lighting.m_GTAO.m_Radius == 1.0f,
			"Discovery and frame planning share tooling inheritance while authored settings retain their value");
		const auto sameId = requestHost(2.0f);
		GGLAB_UNUSED(tick());
		const auto same = runtime.TakeParameterBatchResult(sameId);
		checks.Check(same && same->m_Items[0].m_State == ParameterEffectState::NoChange,
			"A first Host request equal to complete inherited settings is NoChange");
		tooling.m_Settings.m_GTAO->m_Radius = 4.0f;
		runtime.EnableParameterControl();
		checks.Check(control.GetHostParameters().Get(hostRadius, 0.0f) == 2.0f,
			"Queries use an owned tooling snapshot rather than borrowing mutable UI state");
		GGLAB_UNUSED(tick());
		checks.Check(control.GetHostParameters().Get(hostRadius, 0.0f) == 4.0f &&
			probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius == 4.0f,
			"NoChange does not pin a new Host override over later tooling edits");
		if (withLab)
		{
			const std::array masked{ LabParameterValue{ labRadius, 2.5f } };
			const auto maskedId = runtime.RequestParameters(AssignmentRequest(runtime, masked), error);
			GGLAB_UNUSED(tick());
			const auto result = runtime.TakeParameterBatchResult(maskedId);
			checks.Check(result && result->m_Items[0].m_State == ParameterEffectState::Superseded &&
				!result->m_Items[0].m_FirstConsumed && probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius == 4.0f,
				"Tooling alone can mask Lab authoring and must not produce false consumption");
			const std::array unsupported{ LabParameterValue{ hostRadius, 1.5f },
				LabParameterValue{ LabParameterId("unprojected"), 2.0f } };
			checks.Check(runtime.RequestParameters(AssignmentRequest(runtime, unsupported), error) == 0 &&
				!control.GetParameterBatchStatus() && control.GetHostParameters().Get(hostRadius, 0.0f) == 4.0f,
				"An Immediate source without frame projection rejects the complete batch before mutation");
		}
		const auto id = requestHost(1.0f);
		probe.m_SkipParameterFrame = true;
		const auto captures = tooling.m_Captures;
		GGLAB_UNUSED(tick());
		const auto* aborted = control.GetParameterBatchStatus();
		checks.Check(id != 0 && aborted && !aborted->IsTerminal() &&
			aborted->m_Items[0].m_Published && !aborted->m_Items[0].m_FirstConsumed &&
			tooling.m_Captures == captures + 1 && probe.m_ParameterValidationView.m_Lighting.m_GTAO.m_Radius == 1.0f,
			"Host set overrides tooling and an aborted frame cannot consume it");
		tooling.m_Settings.m_GTAO->m_Radius = 3.0f;
		GGLAB_UNUSED(tick());
		const auto done = runtime.TakeParameterBatchResult(id);
		checks.Check(done && done->m_Items[0].m_State == ParameterEffectState::Consumed &&
			probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius == 1.0f &&
			probe.m_ParameterBuiltView.m_PostProcess.m_Bloom.m_Intensity == 0.35f &&
			probe.m_ParameterBuiltView.m_Exposure.m_PreExposure == 1.0f,
			"A successful retry keeps Host precedence and coherent inherited bloom and pre-exposure");
		if (withLab)
		{
			const auto applications = probe.m_ParameterApplications;
			const std::array masked{ LabParameterValue{ labRadius, 2.0f } };
			const auto maskedId = runtime.RequestParameters(AssignmentRequest(runtime, masked), error);
			GGLAB_UNUSED(tick());
			const auto maskedResult = runtime.TakeParameterBatchResult(maskedId);
			checks.Check(maskedResult && maskedResult->m_Items[0].m_State == ParameterEffectState::Superseded &&
				!maskedResult->m_Items[0].m_FirstConsumed && probe.m_ParameterApplications == applications + 1 &&
				probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius == 1.0f,
				"A changed Lab value masked by Host cannot claim frame consumption");
			const auto repeatId = runtime.RequestParameters(AssignmentRequest(runtime, masked), error);
			GGLAB_UNUSED(tick());
			const auto repeat = runtime.TakeParameterBatchResult(repeatId);
			checks.Check(repeat && repeat->m_Items[0].m_State == ParameterEffectState::Superseded &&
				!repeat->m_Items[0].m_FirstConsumed && probe.m_ParameterApplications == applications + 1,
				"An unchanged stored Lab value still needs effective-frame proof instead of false NoChange");
			const std::array matching{ LabParameterValue{ hostRadius, 3.5f }, LabParameterValue{ labRadius, 3.5f } };
			const auto matchId = runtime.RequestParameters(AssignmentRequest(runtime, matching), error);
			GGLAB_UNUSED(tick());
			const auto match = runtime.TakeParameterBatchResult(matchId);
			checks.Check(match && std::ranges::all_of(match->m_Items,
				[](const auto& item) { return item.m_State == ParameterEffectState::Consumed; }),
				"Matching mixed requests are both proven by the same submitted frame");
		}
		runtime.Shutdown();
	}

	void RunHostViewControlScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		probe.m_EnableParameterLab = true;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		ParameterTestTooling tooling;
		tooling.m_Settings.m_TemporalAA = TemporalAASettings{};
		const auto tick = [&]()
			{
				return runtime.Tick({ .m_ApplicationTooling = &tooling });
			};
		const auto find = [](std::span<const HostViewProjectionEntry> projection, std::string_view id)
			{
				const auto found = std::ranges::find(projection, LabParameterId(id), &HostViewProjectionEntry::m_Id);
				GGLAB_ASSERT_MSG(found != projection.end(), "Every Host view binding is projected.");
				return *found;
			};
		tooling.m_Open = true;
		GGLAB_UNUSED(tick());
		const auto& control = *runtime.GetParameterControl();
		const auto& capture = *runtime.GetFrameCaptureCoordinator();
		const auto environment = nlohmann::json::parse(capture.GetEnvironmentJson(), nullptr, false);
		checks.Check(runtime.GetRunIdentity().size() == 32 && environment.is_object() &&
			environment["process"]["runIdentity"] == runtime.GetRunIdentity(),
			"Capture artifacts and session control share the runtime-composed run identity");
		const uint64_t initial = control.GetHostViewRevision();
		const auto* frame = capture.GetLastFrameState();
		checks.Check(!control.IsEnabled() && frame && frame->m_Outcome == FrameCaptureFrameOutcome::Submitted &&
			frame->m_HostViewControl.m_RunIdentity == runtime.GetRunIdentity() &&
			frame->m_HostViewControl.m_ContentIdentity == control.GetContentIdentity() &&
			frame->m_HostViewControl.m_HostViewRevision == initial && frame->m_HostViewProjection.size() == 40 &&
			find(frame->m_HostViewProjection, "view.taa.enabled").m_Source == HostViewValueSource::Tooling &&
			find(frame->m_HostViewProjection, "view.gtao.radius").m_Source == HostViewValueSource::Content &&
			find(frame->m_HostViewProjection, "view.exposure.ev100").m_Source == HostViewValueSource::Camera,
			"Frames record the Host view stamp and layer attribution while parameter tracking stays lazy");
		checks.Check(tooling.m_ViewControl && !control.IsEnabled() &&
			tooling.m_ViewControl->m_PlannedFrame.m_Outcome == ViewControlFrameOutcome::Planned &&
			tooling.m_ViewControl->m_PlannedFrame.m_FrameSerial == frame->m_FrameSerial &&
			tooling.m_ViewControl->m_Control.m_HostViewRevision == initial &&
			find(tooling.m_ViewControl->m_HostView, "view.taa.enabled").m_Source == HostViewValueSource::Tooling,
			"An open tooling frame receives an immutable planned observation without enabling parameter tracking");
		const auto observationRevision = control.GetParameterRevision();
		const auto previousSerial = frame->m_FrameSerial;
		GGLAB_UNUSED(tick());
		checks.Check(tooling.m_ViewControl->m_PreviousFrame &&
			tooling.m_ViewControl->m_PreviousFrame->m_FrameSerial == previousSerial &&
			tooling.m_ViewControl->m_PreviousFrame->m_Outcome == ViewControlFrameOutcome::Submitted &&
			tooling.m_ViewControl->m_PlannedFrame.m_FrameSerial > previousSerial &&
			control.GetParameterRevision() == observationRevision && !control.IsEnabled(),
			"Draw distinguishes the previous submitted frame from the current plan without advancing revisions");

		runtime.EnableParameterControl();
		const auto discovered = control.DescribeHostView();
		checks.Check(control.GetHostViewRevision() == initial && discovered.size() == 40 &&
			find(discovered, "view.taa.enabled").m_InheritedSource == HostViewValueSource::Tooling &&
			!find(discovered, "view.gtao.radius").m_Override,
			"Discovery describes the next frame without advancing the Host view revision");
		std::string error;
		const auto request = [&](float radius)
			{
				const std::array values{ LabParameterValue{ LabParameterId("view.gtao.radius"), radius } };
				const auto id = runtime.RequestParameters(AssignmentRequest(runtime, values), error);
				GGLAB_UNUSED(tick());
				return runtime.TakeParameterBatchResult(id);
			};
		const float inherited = std::get<float>(find(discovered, "view.gtao.radius").m_Value);
		const auto unchanged = request(inherited);
		checks.Check(unchanged && unchanged->m_Items[0].m_State == ParameterEffectState::NoChange &&
			control.GetHostViewRevision() == initial && control.GetHostViewOverrides().empty(),
			"A retained semantic NoChange batch leaves Host overrides and their revision unchanged");
		const auto pinned = request(inherited + 1.0f);
		frame = capture.GetLastFrameState();
		checks.Check(pinned && pinned->m_Items[0].m_State == ParameterEffectState::Consumed &&
			control.GetHostViewRevision() == initial + 1 &&
			frame->m_HostViewControl.m_HostViewRevision == initial + 1 &&
			find(frame->m_HostViewProjection, "view.gtao.radius").m_Source == HostViewValueSource::Host,
			"A committed Host override advances the revision once and is attributed to host");
		const auto repeated = request(inherited + 1.0f);
		checks.Check(repeated && repeated->m_Items[0].m_State == ParameterEffectState::NoChange &&
			control.GetHostViewRevision() == initial + 1,
			"Repeating a present override value keeps the revision");

		tooling.m_Settings.m_GTAO = GTAOSettings{ .m_Radius = 3.0f };
		GGLAB_UNUSED(tick());
		GGLAB_UNUSED(tick());
		frame = capture.GetLastFrameState();
		checks.Check(control.GetHostViewRevision() == initial + 1 &&
			find(frame->m_HostViewProjection, "view.gtao.radius").m_Source == HostViewValueSource::Host &&
			find(frame->m_HostViewProjection, "view.gtao.radius").m_InheritedSource == HostViewValueSource::Tooling &&
			find(frame->m_HostViewProjection, "view.gtao.enabled").m_Source == HostViewValueSource::Tooling,
			"Inherited tooling changes masked by a Host override do not advance the revision");

		probe.m_SkipParameterFrame = true;
		GGLAB_UNUSED(tick());
		checks.Check(capture.GetLastFrameState()->m_Outcome == FrameCaptureFrameOutcome::Aborted,
			"A frame that ends before submission reports an aborted outcome");
		GGLAB_UNUSED(tick());
		checks.Check(tooling.m_ViewControl->m_PreviousFrame &&
			tooling.m_ViewControl->m_PreviousFrame->m_Outcome == ViewControlFrameOutcome::Aborted,
			"A skipped frame remains aborted in the next GUI observation rather than being labeled submitted");

		const auto restart = [&]()
			{
				probe.m_ParameterRuntime->OnExit();
				probe.m_ParameterRuntime->RequestRestartSession();
				probe.m_ParameterRuntime->ProcessPendingCommands();
				probe.m_ParameterRuntime->OnEnter();
				GGLAB_UNUSED(tick());
			};
		const uint64_t content = control.GetContentIdentity();
		restart();
		checks.Check(control.GetContentIdentity() != content && control.GetHostViewOverrides().empty() &&
			control.GetHostViewRevision() == initial + 2,
			"A content activation that discards Host overrides advances the revision once");
		checks.Check(tooling.m_ViewControl->m_PreviousFrame &&
			tooling.m_ViewControl->m_PreviousFrame->m_Control.m_ContentIdentity == content &&
			tooling.m_ViewControl->m_Control.m_ContentIdentity == control.GetContentIdentity(),
			"Content replacement preserves the previous frame's own identity instead of relabeling its evidence");
		restart();
		checks.Check(control.GetHostViewRevision() == initial + 2,
			"A content activation without Host overrides keeps the revision");
		runtime.Shutdown();
	}

	void RunToolingViewControlScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		probe.m_EnableParameterLab = true;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		ParameterTestTooling tooling;
		tooling.m_Open = true;
		tooling.m_Settings.m_GTAO = GTAOSettings{ .m_Radius = 2.0f };
		tooling.m_Settings.m_TemporalAA = TemporalAASettings{};
		const auto tick = [&]()
			{
				return runtime.Tick({ .m_ApplicationTooling = &tooling });
			};
		GGLAB_UNUSED(tick());
		auto& actions = runtime.GetViewControlActions();
		const auto& control = *runtime.GetParameterControl();
		const auto& capture = *runtime.GetFrameCaptureCoordinator();
		const LabParameterId radius("view.gtao.radius");
		std::string error;
		const auto pin = [&](const LabParameterId& parameter, LabParameterType type, LabValue value)
			{
				const std::array states{ HostViewOverrideState{ parameter, type, std::move(value) } };
				const auto id = runtime.RequestParameters(ParameterRequest::HostViewOverrides(
					runtime.GetHostViewControlStamp(), states), error);
				GGLAB_UNUSED(tick());
				return runtime.TakeParameterBatchResult(id);
			};
		checks.Check(ParameterBatchOwner::Tooling(1) != ParameterBatchOwner::Sequence(1) &&
			ParameterBatchOwner::Tooling(0) != ParameterBatchOwner{} &&
			ParameterBatchOwner::Sequence(0) != ParameterBatchOwner{} &&
			ParameterBatchOwner::Sequence(UINT64_MAX) != ParameterBatchOwner::Tooling(UINT64_MAX),
			"Typed tooling, sequence and external owners cannot alias at equal integer identities");
		checks.Check(!control.IsEnabled() && pin(radius, LabParameterType::Float, 1.5f).has_value(),
			"The external client pins a field without read-only GUI observation enabling tracking");
		const std::array equal{ LabParameterValue{ radius, 1.5f } };
		const auto retained = runtime.RequestParameters(AssignmentRequest(runtime, equal), error);
		GGLAB_UNUSED(tick());
		checks.Check(retained && !actions.ReleaseOverride(runtime.GetHostViewControlStamp(), radius, LabParameterType::Float) &&
			actions.GetReleaseStatus().m_Error.starts_with("Busy:") &&
			actions.GetReleaseStatus().m_BlockingOwner == ParameterBatchOwner{} &&
			runtime.GetExternalParameterBatchStatus()->m_BatchId == retained && control.GetParameterBatchCount() == 1,
			"Release reports the blocking external owner and never consumes its retained terminal result");
		checks.Check(runtime.TakeParameterBatchResult(retained).has_value(), "The external owner still collects its own result");
		const auto sequence = runtime.StartFrameSequence({ .m_CameraPathId = "tooling-exclusion" }, error);
		checks.Check(sequence && !actions.ReleaseOverride(runtime.GetHostViewControlStamp(), radius, LabParameterType::Float) &&
			actions.GetReleaseStatus().m_BlockingOwner == ParameterBatchOwner::Sequence(sequence) &&
			control.GetParameterBatchCount() == 0,
			"An active sequence rejects release without admitting a tooling batch and identifies the sequence owner");
		GGLAB_UNUSED(runtime.CancelFrameSequence());

		bool clicked = false;
		tooling.m_OnDraw = [&](const ApplicationToolingFrameContext& context)
			{
				if (!clicked)
				{
					clicked = true;
					checks.Check(context.m_ViewControlActions == &actions && context.m_ViewControl &&
						context.m_ViewControlActions->ReleaseOverride(context.m_ViewControl->m_Control, radius, LabParameterType::Float),
						"Draw receives the in-process action capability and releases against the displayed stamp");
					tooling.m_Settings.m_GTAO->m_Radius = 3.0f;
				}
			};
		GGLAB_UNUSED(tick());
		tooling.m_OnDraw = {};
		const auto pendingId = actions.GetReleaseStatus().m_Batch->m_BatchId;
		checks.Check(clicked && actions.GetReleaseStatus().IsPending() &&
			probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius == 1.5f &&
			!runtime.TakeParameterBatchResult(pendingId) &&
			!control.GetParameterBatchStatus(pendingId, ParameterBatchOwner::Sequence(1)) &&
			!actions.ReleaseOverride(runtime.GetHostViewControlStamp(), radius, LabParameterType::Float) &&
			actions.GetReleaseStatus().m_Batch->m_BatchId == pendingId && control.GetParameterBatchCount() == 1,
			"A one-shot release waits for the next safe frame; duplicate clicks neither queue more work nor lose its owner");
		tooling.m_Open = false;
		probe.m_SkipParameterFrame = true;
		GGLAB_UNUSED(tick());
		checks.Check(actions.GetReleaseStatus().IsPending() &&
			actions.GetReleaseStatus().m_Batch->m_HostViewOperation->m_After &&
			!actions.GetReleaseStatus().m_Batch->m_Items.front().m_FirstConsumed && control.GetParameterBatchCount() == 1,
			"A committed removal on an aborted frame retains its pending effect and result slot until a submitted retry");
		GGLAB_UNUSED(tick());
		const auto& released = *actions.GetReleaseStatus().m_Batch;
		checks.Check(released.m_State == ParameterBatchState::Finished && released.m_HostViewOperation->m_After &&
			released.m_Items.front().m_State == ParameterEffectState::Consumed &&
			released.m_Items.front().m_FirstConsumed == capture.GetLastFrameState()->m_FrameSerial &&
			control.GetParameterBatchCount() == 0 && control.GetHostViewOverrides().empty() &&
			probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius == 3.0f,
			"Closed panels still auto-collect only their result; removal inherits the tooling value at safe commit and proves submission");
		const auto live = runtime.GetHostViewControlStamp();
		for (const auto code : { "ExpectedRunChanged:", "ExpectedContentChanged:", "ExpectedHostViewRevisionMismatch:" })
		{
			auto stale = live;
			if (std::string_view(code) == "ExpectedRunChanged:")
			{
				stale.m_RunIdentity = "another-run";
			}
			else if (std::string_view(code) == "ExpectedContentChanged:")
			{
				++stale.m_ContentIdentity;
			}
			else
			{
				--stale.m_HostViewRevision;
			}
			checks.Check(!actions.ReleaseOverride(stale, radius, LabParameterType::Float) &&
				actions.GetReleaseStatus().m_Error.starts_with(code) && control.GetParameterBatchCount() == 0 &&
				runtime.GetHostViewControlStamp() == live,
				"A stale run/content/revision rejects even an absent removal, without silently refreshing the stamp");
		}
		checks.Check(!actions.ReleaseOverride(live, LabParameterId("radius"), LabParameterType::Float) &&
			actions.GetReleaseStatus().m_Error.starts_with("HostOverrideUnsupported:") &&
			!actions.ReleaseOverride(live, radius, LabParameterType::Bool) &&
			actions.GetReleaseStatus().m_Error.starts_with("HostOverrideUnsupported:") && control.GetParameterBatchCount() == 0,
			"The narrow release path retains descriptor/domain validation");
		checks.Check(actions.ReleaseOverride(live, radius, LabParameterType::Float), "An absent removal can be admitted as NoChange");
		GGLAB_UNUSED(tick());
		checks.Check(actions.GetReleaseStatus().m_Batch->m_Items.front().m_State == ParameterEffectState::NoChange &&
			runtime.GetHostViewControlStamp() == live && control.GetParameterBatchCount() == 0,
			"NoChange retains the control stamp and auto-collects the tooling receipt");
		GGLAB_UNUSED(pin(radius, LabParameterType::Float, 3.0f));
		const auto equalStamp = runtime.GetHostViewControlStamp();
		const auto legacy = control.GetParameterRevision();
		checks.Check(actions.ReleaseOverride(equalStamp, radius, LabParameterType::Float), "An equal-value pin remains releasable");
		GGLAB_UNUSED(tick());
		const auto& equalRelease = *actions.GetReleaseStatus().m_Batch;
		checks.Check(equalRelease.m_HostViewOperation->m_After->m_HostViewRevision == equalStamp.m_HostViewRevision + 1 &&
			equalRelease.m_Items.front().m_State == ParameterEffectState::NoChange &&
			!equalRelease.m_Items.front().m_FirstConsumed && control.GetParameterRevision() == legacy &&
			control.GetParameterBatchCount() == 0,
			"Equal-value release separates its structural control commit from effect NoChange without inventing consumption");
		const std::array scopedPins
		{
			HostViewOverrideState{ LabParameterId("view.taa.enabled"), LabParameterType::Bool, true },
			HostViewOverrideState{ LabParameterId("view.exposure.ev100"), LabParameterType::Float, 14.0f },
		};
		for (const auto& state : scopedPins)
		{
			GGLAB_UNUSED(pin(state.m_Id, state.m_Type, *state.m_Value));
			checks.Check(actions.ReleaseOverride(runtime.GetHostViewControlStamp(), state.m_Id, state.m_Type),
				"TAA and exposure use the same guarded one-field release path");
			GGLAB_UNUSED(tick());
			checks.Check(actions.GetReleaseStatus().m_Batch->m_Items.front().m_State == ParameterEffectState::Consumed &&
				control.GetHostViewOverrides().empty() && control.GetParameterBatchCount() == 0,
				"TAA/exposure release returns to inheritance and submitted-frame proof with no retained GUI record");
		}
		GGLAB_UNUSED(pin(radius, LabParameterType::Float, 1.5f));
		const auto beforeRestart = runtime.GetHostViewControlStamp();
		checks.Check(actions.ReleaseOverride(beforeRestart, radius, LabParameterType::Float), "A release queues before content replacement");
		probe.m_ParameterRuntime->OnExit();
		probe.m_ParameterRuntime->RequestRestartSession();
		probe.m_ParameterRuntime->ProcessPendingCommands();
		probe.m_ParameterRuntime->OnEnter();
		GGLAB_UNUSED(tick());
		const auto& replaced = *actions.GetReleaseStatus().m_Batch;
		checks.Check(replaced.m_State == ParameterBatchState::Failed &&
			replaced.m_HostViewOperation->m_Conflict == "ExpectedContentChanged" &&
			!replaced.m_HostViewOperation->m_After && actions.GetReleaseStatus().m_Expected == beforeRestart &&
			runtime.GetHostViewControlStamp().m_ContentIdentity != beforeRestart.m_ContentIdentity &&
			control.GetParameterBatchCount() == 0,
			"Queued content replacement preserves the old request identity, reports conflict and auto-collects without applying");
		checks.Check(actions.ReleaseOverride(runtime.GetHostViewControlStamp(), radius, LabParameterType::Float),
			"A final release can remain queued until shutdown");
		runtime.Shutdown();
		checks.Check(actions.GetReleaseStatus().m_Batch->m_State == ParameterBatchState::Failed &&
			control.GetParameterBatchCount() == 0 && !actions.GetReleaseStatus().IsPending(),
			"Shutdown terminates and collects pending tooling work even without another Draw");
	}

	void RunToolingReleaseFailureScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		GGLAB_UNUSED(runtime.Tick());
		const LabParameterId radius("view.gtao.radius");
		const std::array pin{ HostViewOverrideState{ radius, LabParameterType::Float, 1.5f } };
		std::string error;
		const auto id = runtime.RequestParameters(ParameterRequest::HostViewOverrides(runtime.GetHostViewControlStamp(), pin), error);
		GGLAB_UNUSED(runtime.Tick());
		GGLAB_UNUSED(runtime.TakeParameterBatchResult(id));
		auto& actions = runtime.GetViewControlActions();
		checks.Check(actions.ReleaseOverride(runtime.GetHostViewControlStamp(), radius, LabParameterType::Float),
			"A fatal-frame test admits a real tooling removal");
		probe.m_FrameEndInjection = FrameEndInjection::FatalFenced;
		checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal &&
			actions.GetReleaseStatus().m_Batch->m_State == ParameterBatchState::Failed &&
			actions.GetReleaseStatus().m_Batch->m_HostViewOperation->m_After &&
			!actions.GetReleaseStatus().m_Batch->m_Items.front().m_FirstConsumed &&
			runtime.GetParameterControl()->GetParameterBatchCount() == 0,
			"Fatal submission retains a committed removal but fails its rendered effect and collects the GUI result");
		runtime.Shutdown();
	}

	void RunHostViewOverrideScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		probe.m_EnableParameterLab = true;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		ParameterTestTooling tooling;
		tooling.m_Settings.m_TemporalAA = TemporalAASettings{};
		const auto tick = [&]()
			{
				return runtime.Tick({ .m_ApplicationTooling = &tooling });
			};
		GGLAB_UNUSED(tick());
		runtime.EnableParameterControl();
		const auto& control = *runtime.GetParameterControl();
		const auto& capture = *runtime.GetFrameCaptureCoordinator();
		const auto find = [](std::span<const HostViewProjectionEntry> projection, std::string_view id)
			{
				const auto found = std::ranges::find(projection, LabParameterId(id), &HostViewProjectionEntry::m_Id);
				GGLAB_ASSERT_MSG(found != projection.end(), "Every Host view binding is projected.");
				return *found;
			};
		const auto present = [&](std::string_view id) -> std::optional<LabValue>
			{
				const auto overrides = control.GetHostViewOverrides();
				const auto found = std::ranges::find(overrides, LabParameterId(id), &LabParameterValue::m_Id);
				return found != overrides.end() ? std::optional(found->m_Value) : std::nullopt;
			};
		const auto pin = [](std::string_view id, LabParameterType type, LabValue value)
			{
				return HostViewOverrideState{ LabParameterId(id), type, std::move(value) };
			};
		const auto release = [](std::string_view id, LabParameterType type)
			{
				return HostViewOverrideState{ LabParameterId(id), type, std::nullopt };
			};
		std::string error;
		const auto apply = [&](std::vector<HostViewOverrideState> states)
			{
				const auto id = runtime.RequestParameters(ParameterRequest::HostViewOverrides(
					runtime.GetHostViewControlStamp(), states), error);
				GGLAB_UNUSED(tick());
				return id ? runtime.TakeParameterBatchResult(id) : std::nullopt;
			};
		const auto revisions = [&](uint64_t before, uint64_t after, const ParameterBatchStatus& batch)
			{
				const auto& operation = *batch.m_HostViewOperation;
				return control.GetHostViewRevision() == after && operation.m_Before &&
					operation.m_Before->m_HostViewRevision == before && operation.m_After &&
					operation.m_After->m_HostViewRevision == after &&
					operation.m_After->m_RunIdentity == runtime.GetRunIdentity() && operation.m_Conflict.empty();
			};
		const LabValue inherited = find(control.DescribeHostView(), "view.gtao.radius").m_Value;
		const LabValue changed = std::get<float>(inherited) + 1.0f;
		constexpr auto Float = LabParameterType::Float;
		constexpr auto Bool = LabParameterType::Bool;

		uint64_t revision = control.GetHostViewRevision();
		uint64_t legacy = control.GetParameterRevision();
		auto result = apply({ pin("view.gtao.radius", Float, inherited) });
		const auto* frame = capture.GetLastFrameState();
		checks.Check(result && result->m_State == ParameterBatchState::Finished &&
			result->m_Items[0].m_State == ParameterEffectState::NoChange && !result->m_Items[0].m_FirstConsumed &&
			revisions(revision, revision + 1, *result) && control.GetParameterRevision() == legacy &&
			result->m_HostViewOperation->m_Changes.size() == 1 &&
			!result->m_HostViewOperation->m_Changes[0].m_Before &&
			LabValuesEqual(*result->m_HostViewOperation->m_Changes[0].m_After, inherited) &&
			present("view.gtao.radius") && LabValuesEqual(*present("view.gtao.radius"), inherited) &&
			LabValuesEqual(result->m_Values[0].m_Value, inherited) &&
			find(frame->m_HostViewProjection, "view.gtao.radius").m_Source == HostViewValueSource::Host,
			"Pinning an inherited-equal value is structural: Host revision only and a NoChange effect");

		revision = control.GetHostViewRevision();
		const std::array equal{ LabParameterValue{ LabParameterId("view.gtao.radius"), inherited } };
		const auto equalId = runtime.RequestParameters(AssignmentRequest(runtime, equal), error);
		GGLAB_UNUSED(tick());
		const auto legacyEqual = runtime.TakeParameterBatchResult(equalId);
		checks.Check(legacyEqual && legacyEqual->m_Items[0].m_State == ParameterEffectState::NoChange &&
			!legacyEqual->m_HostViewOperation && present("view.gtao.radius") &&
			control.GetHostViewRevision() == revision,
			"An equal legacy set keeps an explicit pin and its revision");

		result = apply({ pin("view.gtao.radius", Float, inherited) });
		checks.Check(result && result->m_State == ParameterBatchState::Finished &&
			result->m_Items[0].m_State == ParameterEffectState::NoChange &&
			revisions(revision, revision, *result) && control.GetParameterRevision() == legacy &&
			result->m_HostViewOperation->m_Changes.empty(),
			"Repeating an exact present state is a retained semantic NoChange without counter advances");

		result = apply({ release("view.gtao.radius", Float) });
		checks.Check(result && result->m_Items[0].m_State == ParameterEffectState::NoChange &&
			revisions(revision, revision + 1, *result) && control.GetParameterRevision() == legacy &&
			!present("view.gtao.radius") && result->m_HostViewOperation->m_Changes.size() == 1 &&
			!result->m_HostViewOperation->m_Changes[0].m_After,
			"Removing an override equal to its inherited value is structural only");

		revision = control.GetHostViewRevision();
		result = apply({ release("view.gtao.radius", Float) });
		checks.Check(result && result->m_Items[0].m_State == ParameterEffectState::NoChange &&
			revisions(revision, revision, *result),
			"Removing an absent override is a semantic NoChange");

		// The tooling block inherits Temporal AA disabled.
		result = apply({ pin("view.gtao.radius", Float, changed), pin("view.taa.enabled", Bool, true) });
		checks.Check(result && result->m_State == ParameterBatchState::Finished &&
			std::ranges::all_of(result->m_Items, [](const auto& item)
			{
				return item.m_State == ParameterEffectState::Consumed;
			}) &&
			revisions(revision, revision + 1, *result) && control.GetParameterRevision() == legacy + 1 &&
			result->m_HostViewOperation->m_Changes.size() == 2 && present("view.taa.enabled"),
			"Value-changing entries commit atomically with one revision and Immediate effect proof");

		revision = control.GetHostViewRevision();
		const LabValue replaced = std::get<float>(changed) + 1.0f;
		result = apply({ pin("view.gtao.radius", Float, replaced) });
		checks.Check(result && result->m_Items[0].m_State == ParameterEffectState::Consumed &&
			revisions(revision, revision + 1, *result) && control.GetParameterRevision() == legacy + 2 &&
			LabValuesEqual(*result->m_HostViewOperation->m_Changes[0].m_Before, changed) &&
			LabValuesEqual(*present("view.gtao.radius"), replaced),
			"Replacing a present value is a value change with Immediate effect proof");

		revision = control.GetHostViewRevision();
		legacy = control.GetParameterRevision();
		result = apply({ release("view.gtao.radius", Float) });
		frame = capture.GetLastFrameState();
		checks.Check(result && result->m_Items[0].m_State == ParameterEffectState::Consumed &&
			result->m_Items[0].m_FirstConsumed && LabValuesEqual(result->m_Values[0].m_Value, inherited) &&
			revisions(revision, revision + 1, *result) && control.GetParameterRevision() == legacy + 1 &&
			!present("view.gtao.radius") && present("view.taa.enabled") &&
			find(frame->m_HostViewProjection, "view.gtao.radius").m_Source == HostViewValueSource::Content,
			"Removal resolves inheritance at the safe boundary and proves the inherited value");

		const auto rejects = [&](std::vector<HostViewOverrideState> states, const HostViewControlStamp& expected,
			std::string_view code)
			{
				const std::vector before(control.GetHostViewOverrides().begin(), control.GetHostViewOverrides().end());
				const uint64_t hostViewRevision = control.GetHostViewRevision();
				return runtime.RequestParameters(ParameterRequest::HostViewOverrides(expected, states), error) == 0 &&
					error.starts_with(std::string(code) + ":") && control.GetParameterBatchCount() == 0 &&
					control.GetHostViewRevision() == hostViewRevision &&
					std::ranges::equal(before, control.GetHostViewOverrides(), [](const auto& a, const auto& b)
					{
						return a.m_Id == b.m_Id && LabValuesEqual(a.m_Value, b.m_Value);
					});
			};
		const auto stamp = runtime.GetHostViewControlStamp();
		auto stale = stamp;
		--stale.m_HostViewRevision;
		auto otherContent = stamp;
		++otherContent.m_ContentIdentity;
		auto otherRun = stamp;
		otherRun.m_RunIdentity = "ffffffffffffffffffffffffffffffff";
		checks.Check(rejects({ pin("view.gtao.radius", Float, changed), pin("radius", Float, 2.0f) }, stamp,
			"HostOverrideUnsupported") &&
			rejects({ pin("view.gtao.radius", Bool, true) }, stamp, "HostOverrideUnsupported") &&
			rejects({ pin("view.gtao.radius", Float, 1.0e9f) }, stamp, "HostOverrideInvalid") &&
			rejects({ release("view.gtao.radius", Float), release("view.gtao.radius", Float) }, stamp,
				"HostOverrideInvalid"),
			"Unsupported or invalid entries reject the complete operation before admission");
		checks.Check(rejects({ release("view.gtao.radius", Float) }, stale, "ExpectedHostViewRevisionMismatch") &&
			rejects({ release("view.taa.enabled", Bool) }, otherContent, "ExpectedContentChanged") &&
			rejects({ release("view.taa.enabled", Bool) }, otherRun, "ExpectedRunChanged"),
			"A stale stamp rejects even a would-be NoChange and creates no batch");

		revision = control.GetHostViewRevision();
		const auto retained = runtime.RequestParameters(ParameterRequest::HostViewOverrides(
			stamp, std::vector{ release("view.taa.enabled", Bool) }), error);
		GGLAB_UNUSED(tick());
		const bool busy = runtime.RequestParameters(ParameterRequest::HostViewOverrides(
			runtime.GetHostViewControlStamp(), std::vector{ release("view.taa.enabled", Bool) }), error) == 0 &&
			error.starts_with("Busy:");
		result = runtime.TakeParameterBatchResult(retained);
		checks.Check(busy && result && revisions(revision, revision + 1, *result) && !present("view.taa.enabled"),
			"A retained terminal result holds the external slot shared with set");

		const auto queued = runtime.RequestParameters(ParameterRequest::HostViewOverrides(
			runtime.GetHostViewControlStamp(), std::vector{ pin("view.gtao.radius", Float, changed) }), error);
		const auto* pending = runtime.GetExternalParameterBatchStatus();
		const bool unresolved = pending && pending->m_Values.empty() && !pending->m_HostViewOperation->m_Before;
		probe.m_ParameterRuntime->OnExit();
		probe.m_ParameterRuntime->RequestRestartSession();
		probe.m_ParameterRuntime->ProcessPendingCommands();
		probe.m_ParameterRuntime->OnEnter();
		GGLAB_UNUSED(tick());
		result = runtime.TakeParameterBatchResult(queued);
		checks.Check(queued && unresolved && result && result->m_State == ParameterBatchState::Failed &&
			result->m_Items.empty() && result->m_Failure.starts_with("ExpectedContentChanged:") &&
			result->m_HostViewOperation->m_Conflict == "ExpectedContentChanged" &&
			!result->m_HostViewOperation->m_After && control.GetHostViewOverrides().empty(),
			"A content change while queued ends the operation with a conflict and applies nothing");

		// A capture predicate must be reachable from the live activation at admission.
		const auto live = runtime.GetHostViewControlStamp();
		const auto predicate = [&](uint64_t contentIdentity, uint64_t minHostViewRevision)
			{
				auto required = std::make_shared<RequiredFrameState>();
				required->m_RunIdentity = live.m_RunIdentity;
				required->m_ContentIdentity = contentIdentity;
				required->m_MinHostViewRevision = minHostViewRevision;
				required->m_Values = { { LabParameterId("view.taa.enabled"), Bool, false, std::nullopt } };
				return CapturePlan{ .m_RequiredProducts = { ImageCaptureProduct{} },
					.m_RequiredFrameState = std::move(required) };
			};
		const auto staleContent = runtime.TrySubmitCaptureJob(predicate(live.m_ContentIdentity + 1, live.m_HostViewRevision));
		const auto futureRevision = runtime.TrySubmitCaptureJob(predicate(live.m_ContentIdentity, live.m_HostViewRevision + 1));
		const auto reachable = runtime.TrySubmitCaptureJob(predicate(live.m_ContentIdentity, live.m_HostViewRevision));
		checks.Check(staleContent.m_Reason == CaptureAdmissionReason::RequiredFrameStateUnavailable &&
			futureRevision.m_Reason == CaptureAdmissionReason::RequiredFrameStateUnavailable &&
			reachable.m_Status == CaptureAdmissionStatus::Accepted &&
			runtime.CancelCaptureJob(reachable.m_JobId) == CaptureCancelResult::Cancelled,
			"Capture predicates for another activation or an uncommitted revision reject at admission");
		runtime.Shutdown();
	}
}

namespace gglab
{
	using namespace runtime_test;

	void RunHostViewIntegrationSelfTests(SelfTestContext& context) noexcept
	{
		RuntimeTestDirectory directory("hostview");
		const auto& root = directory.GetPath();
		RunHostParameterScenario(context, root / "host-demo", false);
		RunHostParameterScenario(context, root / "host-lab", true);
		RunEffectiveParameterSettingsScenario(context, root / "tooling-host", false);
		RunEffectiveParameterSettingsScenario(context, root / "tooling-lab", true);
		RunHostViewControlScenario(context, root / "host-view-control");
		RunHostViewOverrideScenario(context, root / "host-view-overrides");
		RunToolingViewControlScenario(context, root / "tooling-view-control");
		RunToolingReleaseFailureScenario(context, root / "tooling-release-failure");
	}
}
