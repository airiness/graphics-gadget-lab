#include "ApplicationInput.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "Demo/DemoManager.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabRuntime/Core/Time.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/RenderCompositionAccess.h"
#include "GGLabRuntime/Graphics/RenderSceneTypes.h"
#include "GGLabRuntime/Scene/Components.h"
#include "GGLabTestCore/SelfTest.h"
#include "GpuFaultLifecycleSelfTests.h"
#include "Lab/LabParameter.h"
#include "Parameters/ParameterBatch.h"
#include "Parameters/ParameterEffectReceipt.h"
#include "Parameters/ParameterRequest.h"
#include "Parameters/SessionParameterControl.h"
#include "RuntimeIntegrationTestCases.h"
#include "RuntimeIntegrationTestServices.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gglab::runtime_test
{
	void RunProtectedParameterScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		probe.m_EnableParameterLab = true;
		probe.m_EnableLightingProfiles = true;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		GGLAB_UNUSED(runtime.Tick());
		runtime.EnableParameterControl();
		auto& lab = *probe.m_ParameterRuntime;
		const auto& control = *runtime.GetParameterControl();
		const auto domain = ParameterControlDomain::Lab;
		const auto initial = runtime.GetDomainControlStamp(domain);
		checks.Check(initial && runtime.GetDomainControlStamp(ParameterControlDomain::LightingProfile),
			"Ready Lab/profile discovery exposes distinct complete domain guards");
		if (!initial)
		{
			runtime.Shutdown();
			return;
		}
		std::string error;
		const LabParameterId radius("radius");
		const auto make = [&](const LabParameterId& id, LabValue value)
			{
				const std::array values{ LabParameterValue{ id, std::move(value) } };
				auto request = AssignmentRequest(runtime, values);
				request.m_ExpectedDomain = runtime.GetDomainControlStamp(domain).value();
				return request;
			};
		auto equal = make(radius, 1.0f);
		const auto noChangeId = runtime.RequestParameters(equal, error);
		GGLAB_UNUSED(runtime.Tick());
		const auto noChange = runtime.TakeParameterBatchResult(noChangeId);
		checks.Check(noChange && noChange->m_DomainOperation->m_Committed &&
			noChange->m_Items[0].m_State == ParameterEffectState::NoChange &&
			noChange->m_DomainOperation->m_Before == noChange->m_DomainOperation->m_After,
			"Protected semantic NoChange retains its receipt without changing control identity");
		auto wrong = make(radius, 2.0f);
		wrong.m_ExpectedDomain->m_RunIdentity = "ffffffffffffffffffffffffffffffff";
		checks.Check(!runtime.RequestParameters(wrong, error) && error.starts_with("ExpectedRunChanged:"),
			"Protected Assign rejects another run before Session admission");
		wrong = make(radius, 2.0f);
		++wrong.m_ExpectedDomain->m_ContentIdentity;
		checks.Check(!runtime.RequestParameters(wrong, error) && error.starts_with("ExpectedContentChanged:"),
			"Protected Assign rejects another activation without reserving a result");
		wrong = make(radius, 2.0f);
		wrong.m_Entries.push_back({ LabParameterId("view.exposure.ev100"), LabParameterType::Float,
			ParameterOperation::Assign, 8.0f });
		checks.Check(!runtime.RequestParameters(wrong, error) && error.starts_with("ParameterDomainUnsupported:") &&
			control.GetParameterBatchCount() == 0,
			"A guarded Lab request cannot acquire Host or cross-domain writes");

		auto owned = make(radius, 2.0f);
		const auto expected = *owned.m_ExpectedDomain;
		const auto id = runtime.RequestParameters(owned, error);
		owned.m_Entries[0].m_Value = 99.0f;
		++owned.m_ExpectedDomain->m_ValuesRevision;
		GGLAB_UNUSED(runtime.Tick());
		const bool busy = !runtime.RequestParameters(make(radius, 3.0f), error) && error.starts_with("Busy:");
		const auto applied = runtime.TakeParameterBatchResult(id);
		checks.Check(busy && applied && applied->m_DomainOperation->m_Expected == expected &&
			applied->m_DomainOperation->m_Committed && applied->m_Items[0].m_FirstConsumed &&
			lab.GetActiveSession()->GetParameters().Get(radius, 0.0f) == 2.0f,
			"Protected intent/guard snapshots survive caller edits and retain the external result slot");

		auto stale = make(radius, 2.0f);
		GGLAB_UNUSED(lab.GetActiveSession()->SetParameter(radius, 3.0f));
		GGLAB_UNUSED(lab.GetActiveSession()->SetParameter(radius, 2.0f));
		checks.Check(!runtime.RequestParameters(stale, error) && error.starts_with("ExpectedParameterControlMismatch:") &&
			control.GetParameterBatchCount() == 0,
			"Direct Lab ABA rejects a stale would-be NoChange before reservations or receipts");
		const auto queued = runtime.RequestParameters(make(radius, 3.0f), error);
		GGLAB_UNUSED(lab.GetActiveSession()->SetParameter(radius, 4.0f));
		GGLAB_UNUSED(runtime.Tick());
		const auto conflict = runtime.TakeParameterBatchResult(queued);
		checks.Check(conflict && !conflict->m_DomainOperation->m_Committed &&
			conflict->m_DomainOperation->m_Conflict == "ExpectedParameterControlMismatch" &&
			conflict->m_Items.empty() && lab.GetActiveSession()->GetParameters().Get(radius, 0.0f) == 4.0f,
			"A queued protected assignment rechecks live intent and preserves a competing writer");
		probe.m_ParameterPrepareCallback = [&]()
			{
				GGLAB_UNUSED(lab.GetActiveSession()->SetParameter(radius, 2.0f));
			};
		const auto prepareId = runtime.RequestParameters(make(LabParameterId("sun-input"), 2.0f), error);
		GGLAB_UNUSED(runtime.Tick());
		const auto duringPrepare = runtime.TakeParameterBatchResult(prepareId);
		checks.Check(duringPrepare && !duringPrepare->m_DomainOperation->m_Committed &&
			duringPrepare->m_DomainOperation->m_Conflict == "ExpectedParameterControlMismatch" &&
			duringPrepare->m_Items.empty(),
			"A source prepare callback cannot invalidate CAS before the final no-callback commit region");

		stale = make(radius, 2.0f);
		auto& world = runtime.GetDemoManager()->GetActiveDemo()->GetWorld();
		const auto atmosphere = world.m_Atmosphere;
		world.m_Atmosphere = AtmosphereSettings{};
		world.MarkLightingAuthoringChanged();
		world.m_Atmosphere = atmosphere;
		world.MarkLightingAuthoringChanged();
		checks.Check(!runtime.RequestParameters(stale, error) && error.starts_with("ExpectedParameterControlMismatch:"),
			"World authoring ABA is visible without relying on a bake generation or refresh sampling");
		stale = make(radius, 2.0f);
		const auto intensity = probe.m_ContentServices.m_EnvironmentLighting->GetEnvironmentLightingSettings().m_Intensity;
		probe.m_ParameterEnvironment->SetIntensity(intensity + 1.0f);
		probe.m_ParameterEnvironment->SetIntensity(intensity);
		checks.Check(!runtime.RequestParameters(stale, error) && error.starts_with("ExpectedParameterControlMismatch:"),
			"Environment setting ABA invalidates the guard independently of effect publication");
		const auto replaced = runtime.RequestParameters(make(radius, 3.0f), error);
		lab.RequestRestartSession();
		GGLAB_UNUSED(runtime.Tick());
		const auto replacement = runtime.TakeParameterBatchResult(replaced);
		checks.Check(replacement && !replacement->m_DomainOperation->m_Committed &&
			replacement->m_DomainOperation->m_Conflict == "ExpectedContentChanged",
			"Unapplied guards cannot follow an unrelated activation replacement");
		runtime.Shutdown();
	}

	void RunParameterRequestScenario(SelfTestContext& checks, const std::filesystem::path& root)
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
		GGLAB_UNUSED(runtime.Tick());
		runtime.EnableParameterControl();
		const auto& control = *runtime.GetParameterControl();
		const LabParameterId radius("view.gtao.radius");
		const auto inherited = control.GetHostParameters().Find(radius)->m_Value;
		const LabValue changed = std::get<float>(inherited) + 1.0f;
		const auto entry = [&](ParameterOperation operation, std::optional<LabValue> value)
			{
				return ParameterRequestEntry{ radius, LabParameterType::Float, operation, std::move(value) };
			};
		std::string error;
		const auto submit = [&](ParameterRequest request)
			{
				const auto id = runtime.RequestParameters(request, error);
				GGLAB_UNUSED(runtime.Tick());
				return id ? runtime.TakeParameterBatchResult(id) : std::nullopt;
			};
		const auto revision = control.GetParameterRevision();
		const auto hostRevision = control.GetHostViewRevision();
		std::vector untyped
		{
			LabParameterValue{ radius, uint32_t(2) },
			LabParameterValue{ LabParameterId("view.taa.resolution_preset"), uint32_t(0) },
		};
		checks.Check(control.ValidateSequenceAssignments(untyped, std::nullopt, error),
			"Sequence integer assignments retain descriptor-based float and enum normalization");
		const auto normalized = control.MakeAssignmentRequest(untyped);
		untyped[0].m_Value = 99.0f;
		checks.Check(normalized.m_Entries.size() == 2 &&
			normalized.m_Entries[0].m_Type == LabParameterType::Float &&
			std::get<float>(*normalized.m_Entries[0].m_Value) == 2.0f &&
			normalized.m_Entries[1].m_Type == LabParameterType::Enum &&
			std::get<int32_t>(*normalized.m_Entries[1].m_Value) == 0 &&
			control.GetParameterBatchCount() == 0 && control.GetParameterRevision() == revision &&
			control.GetHostViewRevision() == hostRevision && control.GetHostViewOverrides().empty(),
			"Assignment projection owns normalized values and types without admitting or changing state");
		const std::array strict{ LabParameterValue{ radius, uint32_t(2) } };
		checks.Check(runtime.RequestParameters(control.MakeAssignmentRequest(strict), error) == 0 &&
			control.GetParameterBatchCount() == 0 && control.GetHostViewRevision() == hostRevision,
			"Ordinary Assign stays strict; only the sequence boundary coerces integer representations");
		auto result = submit({ .m_Entries = { entry(ParameterOperation::Assign, inherited) } });
		checks.Check(result && result->m_State == ParameterBatchState::Finished &&
			result->m_Items[0].m_State == ParameterEffectState::NoChange &&
			!result->m_Items[0].m_FirstConsumed && !result->m_HostViewOperation &&
			control.GetHostViewOverrides().empty() && control.GetParameterRevision() == revision &&
			control.GetHostViewRevision() == hostRevision,
			"Canonical equal Assign preserves inheritance and both revision meanings");

		const auto expected = runtime.GetHostViewControlStamp();
		uint64_t id = 0;
		{
			ParameterRequest request{ .m_Entries = { entry(ParameterOperation::Pin, inherited) },
				.m_Expected = expected };
			id = runtime.RequestParameters(request, error);
			request.m_Entries[0].m_Id = LabParameterId("invalid-after-admission");
			request.m_Entries[0].m_Value = changed;
			request.m_Expected->m_RunIdentity.assign(32, 'f');
			++request.m_Expected->m_ContentIdentity;
			++request.m_Expected->m_HostViewRevision;
		}
		GGLAB_UNUSED(runtime.Tick());
		result = runtime.TakeParameterBatchResult(id);
		checks.Check(result && result->m_State == ParameterBatchState::Finished &&
			result->m_Items[0].m_State == ParameterEffectState::NoChange &&
			control.GetHostViewRevision() == hostRevision + 1 && control.GetParameterRevision() == revision &&
			control.GetHostViewOverrides().size() == 1 &&
			LabValuesEqual(control.GetHostViewOverrides()[0].m_Value, inherited) &&
			result->m_HostViewOperation->m_Expected.m_RunIdentity == expected.m_RunIdentity &&
			result->m_HostViewOperation->m_After->m_ContentIdentity == expected.m_ContentIdentity &&
			LabValuesEqual(*result->m_HostViewOperation->m_Overrides[0].m_Value, inherited),
			"Admitted Pin owns its entries, values and guard after caller mutation and destruction");

		{
			ParameterRequest request{ .m_Entries = { entry(ParameterOperation::Assign, changed) } };
			id = runtime.RequestParameters(request, error);
			request.m_Entries.clear();
		}
		GGLAB_UNUSED(runtime.Tick());
		result = runtime.TakeParameterBatchResult(id);
		checks.Check(result && result->m_State == ParameterBatchState::Finished &&
			result->m_Items[0].m_FirstConsumed && !result->m_HostViewOperation &&
			LabValuesEqual(result->m_Values[0].m_Value, changed) &&
			LabValuesEqual(control.GetHostViewOverrides()[0].m_Value, changed),
			"Admitted Assign owns its value and proves the originally requested submitted frame");

		{
			ParameterRequest request{ .m_Entries = { entry(ParameterOperation::RemoveOverride, std::nullopt) },
				.m_Expected = runtime.GetHostViewControlStamp() };
			id = runtime.RequestParameters(request, error);
			request.m_Entries[0] = entry(ParameterOperation::Pin, changed);
			request.m_Expected.reset();
		}
		GGLAB_UNUSED(runtime.Tick());
		result = runtime.TakeParameterBatchResult(id);
		checks.Check(result && result->m_State == ParameterBatchState::Finished &&
			result->m_Items[0].m_FirstConsumed && control.GetHostViewOverrides().empty() &&
			!result->m_HostViewOperation->m_Overrides[0].m_Value &&
			LabValuesEqual(result->m_Values[0].m_Value, inherited),
			"Admitted removal remains removal and records its resolved inherited target");

		const auto rejects = [&](ParameterRequest request, std::string_view code)
			{
				const auto before = control.GetHostViewRevision();
				return runtime.RequestParameters(request, error) == 0 && error.starts_with(code) &&
					control.GetParameterBatchCount() == 0 && control.GetHostViewOverrides().empty() &&
					control.GetHostViewRevision() == before;
			};
		const auto live = runtime.GetHostViewControlStamp();
		checks.Check(rejects({ .m_Entries = { entry(ParameterOperation::Pin, changed) } },
			"ParameterRequestInvalid:") &&
			rejects({ .m_Entries = { entry(ParameterOperation::Assign, changed),
				entry(ParameterOperation::RemoveOverride, std::nullopt) } }, "ParameterRequestInvalid:") &&
			rejects({ .m_Entries = { entry(ParameterOperation::Assign, changed) }, .m_Expected = live },
				"ParameterRequestInvalid:") &&
			rejects({ .m_Entries = { entry(ParameterOperation::RemoveOverride, changed) }, .m_Expected = live },
				"ParameterRequestInvalid:") &&
			rejects({ .m_Entries = { entry(ParameterOperation::Pin, std::nullopt) }, .m_Expected = live },
				"ParameterRequestInvalid:"),
			"Malformed operations and mixed request families reject atomically without a result slot");
		auto wrongType = entry(ParameterOperation::Assign, changed);
		wrongType.m_Type = LabParameterType::Bool;
		checks.Check(rejects({ .m_Entries = { wrongType } }, "ParameterRequestInvalid:"),
			"Canonical Assign validates its declared type even when its value matches the descriptor");
		checks.Check(rejects({ .m_Entries = {
			{ LabParameterId("radius"), LabParameterType::Float, ParameterOperation::Pin, changed } },
			.m_Expected = live }, "HostOverrideUnsupported:"),
			"The canonical model does not extend Host guards or pinning to Lab parameters");
		auto stale = live;
		--stale.m_HostViewRevision;
		checks.Check(rejects({ .m_Entries = { entry(ParameterOperation::RemoveOverride, std::nullopt) },
			.m_Expected = stale }, "ExpectedHostViewRevisionMismatch:"),
			"A stale canonical guard rejects even an absent removal that would be NoChange");
		result = submit({ .m_Entries = { entry(ParameterOperation::RemoveOverride, std::nullopt) },
			.m_Expected = live });
		checks.Check(result && result->m_State == ParameterBatchState::Finished &&
			result->m_Items[0].m_State == ParameterEffectState::NoChange &&
			control.GetHostViewRevision() == live.m_HostViewRevision,
			"Rejected canonical requests leave capacity available for a retained NoChange result");
		runtime.Shutdown();
	}

	void RunSessionAdmissionScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		probe.m_EnableParameterLab = true;
		FaultFactory factory(probe);
		ApplicationInput input;
		std::atomic<bool> encoderEntered = false;
		std::atomic<bool> releaseEncoder = false;
		GGLabAppRuntime runtime;
		std::string error;
		const std::array host{ LabParameterValue{ LabParameterId("view.exposure.ev100"), 8.0f } };
		const std::array mixed{ host[0], LabParameterValue{ LabParameterId("radius"), 2.0f } };
		checks.Check(runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Admission" }, error) == 0 &&
			!error.empty() && runtime.RequestParameters(AssignmentRequest(runtime, host), error) == 0 && !error.empty() &&
			runtime.CancelFrameSequence() == FrameSequenceCancelVerdict::NotFound &&
			!runtime.TakeParameterBatchResult(1),
			"Session commands reject before composition without creating a sequence or parameter result");
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root,
			[&](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
			{
				encoderEntered = true;
				while (!releaseEncoder)
				{
					std::this_thread::yield();
				}
				return std::vector<uint8_t>{ 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
			}))
		{
			return;
		}
		const auto& parameters = *runtime.GetParameterControl();
		const auto& sequence = *runtime.GetFrameSequenceCoordinator();
		probe.m_ParameterControl = &parameters;
		const auto tickUntil = [&](auto condition)
			{
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
				while (!condition() && std::chrono::steady_clock::now() < deadline)
				{
					probe.m_ReadbackFence = {};
					if (runtime.Tick() != AppRuntimeTickResult::Continue)
					{
						return false;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				return condition();
			};
		checks.Check(runtime.GetDemoManager()->GetActiveDemo()->GetCameraRig().SetCameraPaths({ {
			.m_Id = "SEQ_Admission", .m_Name = "Session admission", .m_Version = 1,
			.m_Keys = {
				{ .m_Frame = 0, .m_Position = { 0, 1, 0 }, .m_Target = { 0, 1, 10 } },
				{ .m_Frame = 2, .m_Position = { 2, 1, 0 }, .m_Target = { 2, 1, 10 } },
			},
		} }), "Session admission uses a production camera path");
		runtime.GetTime()->SetFixedDeltaTime(1.0 / 60.0);
		const auto waiting = runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Admission",
			.m_RequiredContentId = "Missing.AdmissionContent" }, error);
		const auto revision = parameters.GetParameterRevision();
		checks.Check(waiting == 1 && sequence.GetStatus()->m_State == FrameSequenceState::Waiting &&
			runtime.RequestParameters(AssignmentRequest(runtime, host), error) == 0 &&
			error.find("sequence") != std::string::npos &&
			runtime.RequestParameters(AssignmentRequest(runtime, mixed), error) == 0 && parameters.IsEnabled() &&
			parameters.GetParameterRevision() == revision && !parameters.GetParameterBatchStatus(),
			"Waiting sequences enable provenance discovery while rejecting direct batches without reserving parameter state");
		checks.Check(runtime.CancelFrameSequence() == FrameSequenceCancelVerdict::Cancelled &&
			!sequence.IsActive(), "Owner cancellation releases the sequence execution exclusion");
		const auto first = runtime.RequestParameters(AssignmentRequest(runtime, host), error);
		checks.Check(first == 1 && parameters.GetParameterBatchStatus()->m_State == ParameterBatchState::Queued &&
			runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Admission" }, error) == 0 &&
			error.find("pending parameter batch") != std::string::npos &&
			sequence.GetStatus()->m_SequenceId == waiting && parameters.GetParameterBatchStatus()->m_BatchId == first,
			"A queued Host batch rejects direct sequence start without replacing either owner's state");
		checks.Check(tickUntil([&]()
			{
				return parameters.GetParameterBatchStatus()->IsTerminal() &&
					runtime.GetFrameCaptureCoordinator()->GetLastFrameState()->m_Readiness.IsReady();
			}),
			"The admitted Host batch completes and readiness settles before sequence replay");
		checks.Check(runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Admission",
			.m_Events = { { .m_Frame = 0, .m_Values = { host[0] } } } }, error) == 0 &&
			error.find("consume the retained parameter batch result") != std::string::npos &&
			sequence.GetStatus()->m_SequenceId == waiting && parameters.GetParameterBatchStatus()->m_BatchId == first,
			"A retained terminal result rejects an event sequence with consumption guidance and keeps the result");
		const auto running = runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Admission",
			.m_CaptureFrames = { 2 } }, error);
		checks.Check(running == waiting + 1 && parameters.GetParameterBatchStatus()->m_BatchId == first &&
			tickUntil([&]() { return sequence.GetStatus()->m_State == FrameSequenceState::Running; }),
			"A retained terminal parameter result permits a new sequence and remains queryable");
		checks.Check(runtime.RequestParameters(AssignmentRequest(runtime, host), error) == 0 &&
			error.find("sequence") != std::string::npos &&
			runtime.RequestParameters(AssignmentRequest(runtime, mixed), error) == 0 &&
			runtime.TakeParameterBatchResult(first).has_value() && !parameters.GetParameterBatchStatus() &&
			runtime.RequestParameters(AssignmentRequest(runtime, host), error) == 0,
			"Running sequences reject direct parameter writes before and after terminal result consumption");
		checks.Check(tickUntil([&]() { return sequence.GetStatus()->m_State == FrameSequenceState::Finishing && encoderEntered; }) &&
			runtime.RequestParameters(AssignmentRequest(runtime, host), error) == 0 &&
			error.find("sequence") != std::string::npos &&
			runtime.RequestParameters(AssignmentRequest(runtime, mixed), error) == 0 && !parameters.GetParameterBatchStatus(),
			"Finishing retains exclusion while the real sequence writer is blocked inside its codec");
		releaseEncoder = true;
		checks.Check(tickUntil([&]() { return sequence.GetStatus()->IsTerminal(); }) &&
			sequence.GetStatus()->m_State == FrameSequenceState::Completed,
			"Writer completion ends the sequence's execution exclusion");
		const auto second = runtime.RequestParameters(AssignmentRequest(runtime, mixed), error);
		checks.Check(second == first + 1 && tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }) &&
			runtime.TakeParameterBatchResult(second).has_value(),
			"Completed sequences allow a mixed batch without consuming ids for rejected requests");
		const std::array lighting{ LabParameterValue{ LabParameterId("lighting"), uint32_t(2) } };
		const auto deferred = runtime.RequestParameters(AssignmentRequest(runtime, lighting), error);
		GGLAB_UNUSED(runtime.Tick());
		checks.Check(deferred == second + 1 &&
			parameters.GetParameterBatchStatus()->m_State == ParameterBatchState::Applying &&
			runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Admission" }, error) == 0 &&
			error.find("pending parameter batch") != std::string::npos &&
			runtime.CancelFrameSequence() == FrameSequenceCancelVerdict::Finished &&
			!parameters.GetParameterBatchStatus()->IsTerminal(),
			"An Applying Deferred batch excludes sequences until consumption; cancelling an old sequence cannot release it");
		checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }),
			"The Deferred batch eventually reaches its real terminal receipt");
		const auto failing = runtime.StartFrameSequence({ .m_CameraPathId = "Missing.AdmissionPath" }, error);
		checks.Check(failing == running + 1 &&
			tickUntil([&]() { return sequence.GetStatus()->m_State == FrameSequenceState::Failed; }) &&
			runtime.TakeParameterBatchResult(deferred).has_value(),
			"A failed sequence releases exclusion and preserves the retained Deferred receipt");
		runtime.HandleHostEvent({ .m_Type = AppHostEventType::Suspended });
		const auto suspended = runtime.RequestParameters(AssignmentRequest(runtime, host), error);
		checks.Check(suspended == deferred + 1 && runtime.Tick() == AppRuntimeTickResult::Suspended &&
			parameters.GetParameterBatchStatus()->m_State == ParameterBatchState::Queued &&
			runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Admission" }, error) == 0,
			"Suspension preserves queued session commands and their admission exclusion until resume");
		runtime.HandleHostEvent({ .m_Type = AppHostEventType::Resumed });
		checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }) &&
			runtime.TakeParameterBatchResult(suspended).has_value(),
			"Resume completes the admitted suspended batch");
		const std::array fatalValues{ LabParameterValue{ LabParameterId("view.exposure.ev100"), 9.0f } };
		const auto fatal = runtime.RequestParameters(AssignmentRequest(runtime, fatalValues), error);
		probe.m_FrameEndInjection = FrameEndInjection::FatalFenced;
		checks.Check(fatal != 0 && runtime.Tick() == AppRuntimeTickResult::Fatal &&
			runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Admission" }, error) == 0 &&
			runtime.RequestParameters(AssignmentRequest(runtime, host), error) == 0 &&
			parameters.GetParameterBatchStatus()->IsTerminal(),
			"A failed runtime rejects new session work without rewriting its terminal parameter receipt");
		runtime.Shutdown();
		checks.Check(runtime.TakeParameterBatchResult(fatal).has_value() &&
			!runtime.TakeParameterBatchResult(fatal) &&
			runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Admission" }, error) == 0 &&
			runtime.RequestParameters(AssignmentRequest(runtime, host), error) == 0,
			"Post-shutdown consumption returns the real retained result once and cannot reopen session admission");
	}

	void RunSessionExitAdmissionScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		const std::array values{ LabParameterValue{ LabParameterId("view.exposure.ev100"), 8.0f } };
		std::string error;
		const auto id = runtime.RequestParameters(AssignmentRequest(runtime, values), error);
		runtime.HandleHostEvent({ .m_Type = AppHostEventType::ExitRequested });
		checks.Check(id != 0 && !runtime.CanRequestParameters(error) && !error.empty() &&
			runtime.RequestParameters(AssignmentRequest(runtime, values), error) == 0 &&
			runtime.StartFrameSequence({ .m_CameraPathId = "Missing.ExitPath" }, error) == 0 &&
			runtime.GetParameterControl()->GetParameterBatchStatus()->m_State == ParameterBatchState::Queued &&
			!runtime.TakeParameterBatchResult(id) && runtime.Tick() == AppRuntimeTickResult::Exit,
			"ExitRequested closes admission while preserving the queued parameter owner's actual state");
		runtime.Shutdown();
		const auto result = runtime.TakeParameterBatchResult(id);
		checks.Check(result && result->IsTerminal() && !result->m_Failure.empty() &&
			runtime.CancelFrameSequence() == FrameSequenceCancelVerdict::NotFound,
			"Graceful shutdown closes the unapplied batch and leaves its receipt available through AppRuntime");
	}

	void RunParameterFeedbackScenario(SelfTestContext& checks, const std::filesystem::path& root)
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
		// Standalone source tests may inject frame evidence. The AppRuntime-owned
		// source is exposed read-only and receives evidence only from its frame owner.
		const auto& environment = *probe.m_ContentServices.m_RenderServices.m_Environment;
		SessionParameterControl source(*probe.m_ContentServices.m_RenderServices.m_Environment);
		source.SynchronizeContent(runtime.GetDemoManager()->GetActiveDemo(),
			runtime.GetDemoManager()->GetActiveContentIdentity(), "FaultLifecycle.Demo", false);
		const std::array values{ LabParameterValue{ LabParameterId("lighting"), uint32_t(2) } };
		std::string error;
		const auto id = source.RequestParameters(AssignmentRequest(source, values), error);
		source.OnFrameReady(probe.m_ParameterBuiltSerial + 1);
		const auto generation = environment.GetBakeRequestGeneration();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (environment.GetBakingStatus().m_ActiveGeneration != generation &&
			std::chrono::steady_clock::now() < deadline)
		{
			probe.m_ReadbackFence = {};
			GGLAB_UNUSED(runtime.Tick());
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		const auto serial = probe.m_ParameterBuiltSerial;
		source.OnFrameReady(serial);
		source.OnFrameBuilt(serial, environment.GetBakingStatus().m_ActiveGeneration,
			probe.m_ParameterBuiltView);
		const auto published = source.GetParameterBatchStatus()->m_Items.front().m_Published;
		checks.Check(id != 0 && published && !source.GetParameterBatchStatus()->IsTerminal(),
			"The standalone source observes an actual Runtime Deferred publication before feedback injection");
		source.OnFrameSubmitted({ .m_RenderSceneStatus = RenderSceneBuildStatus::GpuUploadFailed,
			.m_SubmittedFence = probe.m_LastEndFence, .m_RenderFrameSerial = serial });
		checks.Check(!source.GetParameterBatchStatus()->m_Items.front().m_FirstConsumed &&
			source.GetParameterBatchStatus()->m_Items.front().m_Published == published,
			"A failed scene upload cannot consume a lighting publication even with a valid fence and matching serial");
		source.OnFrameSubmitted({ .m_RenderSceneStatus = RenderSceneBuildStatus::Ready,
			.m_SubmittedFence = probe.m_LastEndFence, .m_RenderFrameSerial = serial });
		checks.Check(!source.GetParameterBatchStatus()->m_Items.front().m_FirstConsumed,
			"Failed scene feedback discards its consumed-state evidence instead of allowing a later same-frame replay");
		GGLAB_UNUSED(runtime.Tick());
		source.OnFrameReady(probe.m_ParameterBuiltSerial);
		source.OnFrameBuilt(probe.m_ParameterBuiltSerial, environment.GetBakingStatus().m_ActiveGeneration,
			probe.m_ParameterBuiltView);
		source.OnFrameSubmitted({ .m_RenderSceneStatus = RenderSceneBuildStatus::Ready,
			.m_SubmittedFence = probe.m_LastEndFence, .m_RenderFrameSerial = probe.m_ParameterBuiltSerial });
		const auto done = source.TakeParameterBatchResult(id);
		checks.Check(done && done->m_Items.front().m_State == ParameterEffectState::Consumed &&
			done->m_Items.front().m_FirstConsumed > published,
			"Only fresh built evidence and a successful matching submission complete the Deferred receipt");
		source.Finish();
		runtime.Shutdown();
	}

	void RunPendingParameterBatchScenario(SelfTestContext& checks, const std::filesystem::path& root)
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
		probe.m_EnableTemporalHistory = false;
		GGLAB_UNUSED(runtime.Tick());
		auto& environment = *probe.m_ContentServices.m_RenderServices.m_Environment;
		auto* demo = runtime.GetDemoManager()->GetActiveDemo();
		const auto content = runtime.GetDemoManager()->GetActiveContentIdentity();
		const auto owner = ParameterBatchOwner::Sequence(1);
		const auto other = ParameterBatchOwner::Sequence(2);
		SessionParameterControl source(environment);
		source.SynchronizeContent(demo, content, "FaultLifecycle.Demo", false);
		uint64_t serial = 0;
		std::string error;
		const auto request = [&](std::string_view name, LabValue value)
			{
				const std::array values{ LabParameterValue{ LabParameterId(name), value } };
				return source.RequestParameters(AssignmentRequest(source, values), error, owner);
			};
		const auto status = [&](uint64_t id) { return source.GetParameterBatchStatus(id, owner); };
		const auto begin = [&]() { source.OnFrameReady(++serial); };
		// This standalone coordinator test injects view/submission evidence, while
		// domain requests and publication use the production lighting scheduler.
		const auto build = [&]()
			{
				auto view = probe.m_ParameterBuiltView;
				source.ApplyViewSettings(view, false);
				source.OnFrameBuilt(serial, environment.GetBakingStatus().m_ActiveGeneration, view);
			};
		const auto submit = [&]()
			{
				source.OnFrameSubmitted({ .m_RenderSceneStatus = RenderSceneBuildStatus::Ready,
					.m_SubmittedFence = probe.m_LastEndFence, .m_RenderFrameSerial = serial });
			};
		const auto waitForLighting = [&]()
			{
				const auto target = environment.GetBakeRequestGeneration();
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
				while (environment.GetBakingStatus().m_ActiveGeneration != target &&
					std::chrono::steady_clock::now() < deadline)
				{
					probe.m_ReadbackFence = {};
					if (runtime.Tick() != AppRuntimeTickResult::Continue)
					{
						return false;
					}
					std::this_thread::yield();
				}
				return environment.GetBakingStatus().m_ActiveGeneration == target;
			};
		const auto first = request("lighting", uint32_t(2));
		checks.Check(first && !request("view.exposure.ev100", 9.0f),
			"An unapplied transaction retains the single source reservation");
		begin();
		const auto* retained = status(first);
		const auto applied = retained->m_Items[0].m_Applied;
		const auto generation = retained->m_Items[0].m_Identity.m_Generation;
		const auto immediate = request("view.exposure.ev100", 9.0f);
		begin();
		build();
		auto frozen = source.CaptureFrameState(serial, environment.GetBakeRequestGeneration(),
			environment.GetBakingStatus().m_ActiveGeneration);
		const auto builtJson = nlohmann::json::parse(SerializeParameterFrameState(frozen));
		checks.Check(frozen.m_Receipts.size() == 2 &&
			!frozen.m_Receipts.back().m_Item.m_FirstConsumed &&
			!frozen.m_RequestedLighting.m_Profile && !frozen.m_ActiveLighting.m_Profile,
			"Build-time parameter snapshots retain outstanding receipts and leave unknown profile identity null");
		submit();
		source.CompleteFrameState(frozen);
		const auto submittedJson = SerializeParameterFrameState(frozen);
		checks.Check(frozen.m_Receipts.back().m_Item.m_FirstConsumed == serial &&
			nlohmann::json::parse(submittedJson)["values"] == builtJson["values"],
			"Submission completes exact frozen receipts without resampling parameter inputs");
		checks.Check(immediate && retained == status(first) && !retained->IsTerminal() &&
			retained->m_Items[0].m_Applied == applied && status(immediate)->IsTerminal() &&
			status(immediate)->m_Items[0].m_State == ParameterEffectState::Consumed &&
			status(immediate)->m_Revision > retained->m_Revision,
			"An unrelated later revision consumes Immediate work without superseding pending Deferred work");
		checks.Check(!source.GetParameterBatchStatus() && !source.GetParameterBatchStatus(first, other) &&
			!source.TakeParameterBatchResult(immediate) && !source.TakeParameterBatchResult(immediate, other),
			"External and foreign owners cannot query or collect internal receipts");
		source.FinishParameterBatches(other);
		const std::array foreign{ LabParameterValue{ LabParameterId("radius"), 2.0f } };
		checks.Check(!retained->IsTerminal() && !source.RequestParameters(AssignmentRequest(source, foreign), error, other) &&
			!source.RequestParameters(AssignmentRequest(source, foreign), error),
			"A foreign owner cannot cancel or acquire an occupied session lane");
		checks.Check(source.TakeParameterBatchResult(immediate, owner).has_value() && retained == status(first),
			"Collecting another record preserves borrowed status addresses");
		source.CompleteFrameState(frozen);
		checks.Check(SerializeParameterFrameState(frozen) == submittedJson,
			"Collecting live receipts cannot change an owned submitted parameter snapshot");
		const auto shared = request("lighting", uint32_t(2));
		begin();
		checks.Check(shared && !status(shared)->IsTerminal() &&
			status(shared)->m_Items[0].m_Identity.m_Generation == generation &&
			environment.GetBakeRequestGeneration() == generation,
			"Two batches may independently wait on the same pending domain target without a redundant request");
		checks.Check(waitForLighting(), "The shared target reaches actual Runtime publication");
		begin();
		build();
		const auto publication = retained->m_Items[0].m_Published;
		source.OnFrameSubmitted({ .m_RenderSceneStatus = RenderSceneBuildStatus::GpuUploadFailed,
			.m_SubmittedFence = probe.m_LastEndFence, .m_RenderFrameSerial = serial });
		submit();
		checks.Check(publication && !retained->IsTerminal() && !status(shared)->IsTerminal(),
			"Failed submission discards candidates for all waiting batches and cannot be replayed");
		begin();
		build();
		probe.m_ParameterEnvironment->RequestRebake();
		submit();
		const auto firstDone = source.TakeParameterBatchResult(first, owner);
		const auto sharedDone = source.TakeParameterBatchResult(shared, owner);
		checks.Check(firstDone && sharedDone && firstDone->m_BatchId != sharedDone->m_BatchId &&
			firstDone->m_Items[0].m_State == ParameterEffectState::Consumed &&
			sharedDone->m_Items[0].m_State == ParameterEffectState::Consumed &&
			firstDone->m_Items[0].m_Published == publication &&
			firstDone->m_Items[0].m_FirstConsumed == serial &&
			sharedDone->m_Items[0].m_FirstConsumed == serial,
			"Independent receipts consume frozen matching evidence despite a newer live request after build");

		const auto aborted = request("view.exposure.ev100", 10.0f);
		begin();
		build();
		const auto abortedPublication = status(aborted)->m_Items[0].m_Published;
		const auto laterLighting = request("lighting", uint32_t(4));
		begin();
		build();
		submit();
		const auto retried = source.TakeParameterBatchResult(aborted, owner);
		checks.Check(retried && retried->m_Items[0].m_State == ParameterEffectState::Consumed &&
			retried->m_Items[0].m_Published == abortedPublication &&
			retried->m_Items[0].m_FirstConsumed > abortedPublication,
			"A later proven Deferred operation is not misattributed to an older Immediate retry");
		const auto replacement = request("lighting", uint32_t(8));
		begin();
		const auto superseded = source.TakeParameterBatchResult(laterLighting, owner);
		checks.Check(superseded && superseded->m_Items[0].m_State == ParameterEffectState::Superseded &&
			superseded->m_Items[0].m_Applied && !superseded->m_Items[0].m_FirstConsumed,
			"A committed different assignment permanently supersedes an older unconsumed request");
		checks.Check(waitForLighting(), "The replacement domain target becomes active");
		const auto broken = request("broken-lighting", uint32_t(16));
		begin();
		build();
		checks.Check(status(broken)->m_State == ParameterBatchState::Failed &&
			status(replacement)->m_Items[0].m_State == ParameterEffectState::Superseded &&
			!status(replacement)->m_Items[0].m_Published,
			"A newer target before build supersedes an older still-active generation across parameter bindings");
		GGLAB_UNUSED(source.TakeParameterBatchResult(broken, owner));
		GGLAB_UNUSED(source.TakeParameterBatchResult(replacement, owner));

		const auto old = request("view.exposure.ev100", 11.0f);
		begin();
		build();
		const auto oldPublication = status(old)->m_Items[0].m_Published;
		const auto away = request("view.exposure.ev100", 12.0f);
		begin();
		const auto back = request("view.exposure.ev100", 11.0f);
		source.OnFrameReady(serial);
		checks.Check(status(back)->m_State == ParameterBatchState::Queued,
			"Repeating one Ready boundary cannot apply a second reservation in that frame");
		begin();
		build();
		submit();
		checks.Check(status(old)->m_Items[0].m_State == ParameterEffectState::Superseded &&
			status(old)->m_Items[0].m_Published == oldPublication &&
			status(away)->m_Items[0].m_State == ParameterEffectState::Superseded &&
			status(back)->m_Items[0].m_State == ParameterEffectState::Consumed,
			"An away-and-back assignment before build cannot resurrect either overwritten receipt");
		GGLAB_UNUSED(source.TakeParameterBatchResult(old, owner));
		GGLAB_UNUSED(source.TakeParameterBatchResult(away, owner));
		GGLAB_UNUSED(source.TakeParameterBatchResult(back, owner));

		std::vector<uint64_t> ids;
		const auto revision = source.GetParameterRevision();
		for (size_t index = 0; index < SessionParameterMaxBatches; ++index)
		{
			ids.push_back(request("view.exposure.ev100", 11.0f));
			begin();
			build();
		}
		const auto applications = probe.m_ParameterApplications;
		checks.Check(source.GetParameterBatchCount() == SessionParameterMaxBatches &&
			std::ranges::all_of(ids, [&](auto id)
			{
				return id && status(id)->m_Items[0].m_State == ParameterEffectState::NoChange;
			}) && !request("radius", 2.0f) && source.GetParameterRevision() == revision &&
			probe.m_ParameterApplications == applications,
			"Retained terminal results count toward a hard bound; overflow neither reserves nor mutates");
		GGLAB_UNUSED(source.TakeParameterBatchResult(ids.front(), owner));
		const auto recovered = request("radius", 2.0f);
		checks.Check(recovered == ids.back() + 1,
			"Explicit collection restores capacity and rejected admission consumes no batch identity");
		probe.m_ParameterEffectCallback = [&]()
			{
				checks.Check(!request("view.exposure.ev100", 13.0f) &&
					!source.TakeParameterBatchResult(ids.back(), owner),
					"Effect callbacks cannot reenter admission or invalidate records during transaction application");
				source.FinishParameterBatches(owner);
			};
		begin();
		checks.Check(status(recovered)->m_State == ParameterBatchState::Applying,
			"A reentrant owner finish cannot partially cancel the committing transaction");
		source.FinishParameterBatches(owner);
		checks.Check(status(recovered)->IsTerminal() && !source.HasPendingParameterBatches() &&
			source.GetParameterBatchStatuses(other).empty(),
			"Owner finish closes unresolved work while retaining independently collectible results");
		for (const auto& batch : source.GetParameterBatchStatuses(owner))
		{
			GGLAB_UNUSED(source.TakeParameterBatchResult(batch.m_BatchId, owner));
		}
		checks.Check(source.GetParameterBatchCount() == 0, "Every retained record is collected exactly once");
		source.Finish();

		for (uint32_t ending = 0; ending < 4; ++ending)
		{
			SessionParameterControl pending(environment);
			pending.SynchronizeContent(demo, content, "FaultLifecycle.Demo", false);
			const std::array lighting{ LabParameterValue{ LabParameterId("lighting"), uint32_t(32 + ending) } };
			const auto waiting = pending.RequestParameters(AssignmentRequest(pending, lighting), error, owner);
			pending.OnFrameReady(1);
			const auto queued = pending.RequestParameters(AssignmentRequest(pending, foreign), error, owner);
			const auto stamp = pending.GetParameterBatchStatus(waiting, owner)->m_Items[0].m_Applied;
			if (ending == 0)
			{
				pending.FinishParameterBatches(owner);
			}
			else if (ending == 1)
			{
				pending.SynchronizeContent(nullptr, 0, {}, true);
			}
			else if (ending == 2)
			{
				pending.OnRuntimeFailed("Injected pending-transaction runtime failure.");
			}
			else
			{
				pending.Finish();
			}
			const auto closed = pending.TakeParameterBatchResult(waiting, owner);
			const auto cancelled = pending.TakeParameterBatchResult(queued, owner);
			const auto expected = ending == 1 ? ParameterEffectState::Superseded :
				ending == 2 ? ParameterEffectState::Failed : ParameterEffectState::EffectNotReached;
			checks.Check(waiting && queued && closed && cancelled &&
				closed->m_Items[0].m_State == expected && closed->m_Items[0].m_Applied == stamp &&
				!closed->m_Items[0].m_FirstConsumed && cancelled->m_State == ParameterBatchState::Failed &&
				pending.GetParameterBatchCount() == 0,
				"Owner/content/failure/shutdown closes all pending receipts and cancels only the queued reservation");
			pending.Finish();
		}
		runtime.Shutdown();
	}

	void RunBatchPreparationScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		probe.m_EnableParameterLab = true;
		probe.m_EnableLightingProfiles = true;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		auto& parameters = *runtime.GetParameterControl();
		runtime.EnableParameterControl();
		probe.m_ParameterControl = &parameters;
		auto& lab = *probe.m_ParameterRuntime;
		auto& world = runtime.GetDemoManager()->GetActiveDemo()->GetWorld();
		auto& lighting = *runtime.GetRenderComposition()->GetWorldLightingControl();
		const auto& environment = *probe.m_ContentServices.m_RenderServices.m_Environment;
		const auto source = lighting.GetWorldLightingPublication().m_Requested;
		std::optional<entt::entity> sun;
		for (const auto entity : world.GetRegistry().view<components::LightComponent>())
		{
			if (world.GetRegistry().get<components::LightComponent>(entity).m_WorldSun)
			{
				sun = entity;
				break;
			}
		}
		checks.Check(source && sun && parameters.GetLightingProfiles().size() == 2,
			"Batch preparation fixture binds real Lab, Host and World Lighting sources");
		if (!source || !sun)
		{
			runtime.Shutdown();
			return;
		}
		const std::array values
		{
			LabParameterValue{ LabParameterId("radius"), 2.0f },
			LabParameterValue{ LabParameterId("environment.profile"), int32_t(1) },
			LabParameterValue{ LabParameterId("view.exposure.ev100"), 11.0f },
			LabParameterValue{ LabParameterId("view.gtao.radius"), 1.0f },
		};
		std::string error;
		const auto initialExposure = parameters.GetHostParameters().Get(LabParameterId("view.exposure.ev100"), 0.0f);
		const auto applications = probe.m_ParameterApplications;
		const auto labRevision = lab.GetParameterRevision();
		const auto unchanged = [&]()
			{
				return probe.m_ParameterApplications == applications && lab.GetParameterRevision() == labRevision &&
					lab.GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f) == 1.0f &&
					parameters.GetHostParameters().Get(LabParameterId("view.exposure.ev100"), 0.0f) == initialExposure &&
					probe.m_ParameterBuiltView.m_Exposure.m_ManualEV100 == initialExposure;
			};

		const std::array labValues{ LabParameterValue{ LabParameterId("radius"), 2.0f } };
		const uint64_t independentId = 1;
		checks.Check(lab.QueueParameterBatch(independentId, labValues, error) &&
			runtime.RequestParameters(AssignmentRequest(runtime, values), error) == 0 &&
			!parameters.GetParameterBatchStatus() && unchanged(),
			"Busy source admission never allocates a Session receipt or changes live values");
		auto independentPlan = lab.PrepareParameterBatch(independentId, labValues, error);
		checks.Check(independentPlan.has_value() && unchanged(),
			"Failed admission preserves the source's pre-existing same-numbered reservation");
		independentPlan.reset();
		lab.CancelParameterBatch(independentId);

		const auto cancelled = runtime.RequestParameters(AssignmentRequest(runtime, values), error);
		const auto cancelledRevision = parameters.GetParameterRevision();
		const auto cancelledGeneration = environment.GetBakeRequestGeneration();
		auto labPlan = lab.PrepareParameterBatch(cancelled, labValues, error);
		checks.Check(labPlan && labPlan->HasChanges() && unchanged() &&
			environment.GetBakeRequestGeneration() == cancelledGeneration,
			"Lab preparation changes neither live values, handler counts nor Runtime requests");
		labPlan.reset();
		labPlan = lab.PrepareParameterBatch(cancelled, labValues, error);
		checks.Check(labPlan.has_value() && unchanged(),
			"Discarding a Lab plan leaves the same command reservation available for preparation");
		labPlan.reset();
		lab.CancelParameterBatch(cancelled);
		GGLAB_UNUSED(runtime.Tick());
		const auto lostReservation = runtime.TakeParameterBatchResult(cancelled);
		checks.Check(cancelled != 0 && lostReservation && lostReservation->m_State == ParameterBatchState::Failed &&
			lostReservation->m_Items.empty() && lostReservation->m_Revision == 0 && unchanged() &&
			parameters.GetParameterRevision() == cancelledRevision &&
			environment.GetBakeRequestGeneration() == cancelledGeneration,
			std::format("A lost Lab reservation rejects preparation before mutation "
				"(id={}, failure={}, unchanged={}, revision={}/{}, generation={}/{})",
				cancelled, lostReservation ? lostReservation->m_Failure : error, unchanged(),
				parameters.GetParameterRevision(), cancelledRevision,
				environment.GetBakeRequestGeneration(), cancelledGeneration));

		const auto missing = runtime.RequestParameters(AssignmentRequest(runtime, values), error);
		auto& light = world.GetRegistry().get<components::LightComponent>(*sun);
		const auto originalSun = light.m_WorldSun;
		light.m_WorldSun.reset();
		// This is an external authored edit, not part of the accepted batch.
		lighting.SynchronizeWorldLighting(world, source->m_SessionIdentity);
		runtime.EnableParameterControl();
		const auto missingRevision = parameters.GetParameterRevision();
		const auto missingGeneration = environment.GetBakeRequestGeneration();
		GGLAB_UNUSED(runtime.Tick());
		const auto missingTarget = runtime.TakeParameterBatchResult(missing);
		checks.Check(missing != 0 && missingTarget && missingTarget->m_State == ParameterBatchState::Failed &&
			missingTarget->m_Failure.starts_with("LightingPreparationFailed:") &&
			missingTarget->m_Items.empty() && missingTarget->m_Revision == 0 && unchanged() &&
			parameters.GetParameterRevision() == missingRevision &&
			environment.GetBakeRequestGeneration() == missingGeneration,
			"A sun lost after admission rejects the entire prepared batch without applying exposure or calling the Lab handler");

		light.m_WorldSun = originalSun;
		lighting.SynchronizeWorldLighting(world, source->m_SessionIdentity);
		runtime.EnableParameterControl();
		const auto revision = parameters.GetParameterRevision();
		probe.m_ExpectedCommitGeneration = environment.GetBakeRequestGeneration() + 1;
		probe.m_ObserveParameterCommit = true;
		const auto accepted = runtime.RequestParameters(AssignmentRequest(runtime, values), error);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (accepted != 0 && !parameters.GetParameterBatchStatus()->IsTerminal() &&
			std::chrono::steady_clock::now() < deadline)
		{
			probe.m_ReadbackFence = {};
			if (runtime.Tick() != AppRuntimeTickResult::Continue)
			{
				break;
			}
		}
		const auto committed = runtime.TakeParameterBatchResult(accepted);
		checks.Check(committed && committed->m_State == ParameterBatchState::Finished &&
			probe.m_ParameterCommitWasComplete && probe.m_ParameterApplications == applications + 1 &&
			parameters.GetParameterRevision() == revision + 1 && lab.GetParameterRevision() == labRevision + 1,
			std::format("A successful mixed batch installs all sources before effects "
				"(id={}, failure={}, observed={}, applications={}/{}, revision={}/{}, labRevision={}/{})",
				accepted, committed ? committed->m_Failure : error, probe.m_ParameterCommitWasComplete,
				probe.m_ParameterApplications, applications + 1, parameters.GetParameterRevision(), revision + 1,
				lab.GetParameterRevision(), labRevision + 1));
		checks.Check(committed && committed->m_Items.size() == 4 &&
			std::ranges::all_of(committed->m_Items, [](const auto& item)
			{
				return item.m_Parameter == LabParameterId("radius") ?
					item.m_State == ParameterEffectState::Superseded && !item.m_FirstConsumed :
					item.m_State == ParameterEffectState::Consumed;
			}) &&
			committed->m_Items[1].m_Identity.m_Generation == probe.m_ExpectedCommitGeneration &&
			probe.m_ParameterBuiltView.m_Exposure.m_ManualEV100 == 11.0f &&
			probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius == 1.0f &&
			lab.GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f) == 2.0f,
			std::format("Host precedence survives an inherited NoChange becoming changed "
				"(hostRadius={}, labRadius={}, exposure={}, items={})",
				probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius,
				lab.GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f),
				probe.m_ParameterBuiltView.m_Exposure.m_ManualEV100, committed ? committed->m_Items.size() : 0));
		runtime.Shutdown();
	}
}

namespace gglab
{
	using namespace runtime_test;

	void RunParameterRequestIntegrationSelfTests(SelfTestContext& context) noexcept
	{
		RuntimeTestDirectory directory("parameters");
		const auto& root = directory.GetPath();
		RunParameterRequestScenario(context, root / "parameter-request");
		RunProtectedParameterScenario(context, root / "protected-parameters");
		RunBatchPreparationScenario(context, root / "batch-preparation");
		RunSessionAdmissionScenario(context, root / "session-admission");
		RunSessionExitAdmissionScenario(context, root / "session-exit-admission");
		RunParameterFeedbackScenario(context, root / "parameter-feedback");
		RunPendingParameterBatchScenario(context, root / "pending-parameter-batches");
	}
}
