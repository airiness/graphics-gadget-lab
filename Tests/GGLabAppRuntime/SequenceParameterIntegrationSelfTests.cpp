#include "ApplicationInput.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureReadiness.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "Demo/DemoManager.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Time.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabTestCore/SelfTest.h"
#include "GpuFaultLifecycleSelfTests.h"
#include "Lab/LabParameter.h"
#include "Parameters/ParameterBatch.h"
#include "Parameters/ParameterEffectReceipt.h"
#include "RuntimeIntegrationTestCases.h"
#include "RuntimeIntegrationTestServices.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gglab::runtime_test
{
	void RunSequenceParameterEventScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		probe.m_EnableParameterLab = true;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root,
			[](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
			{
				return std::vector<uint8_t>{ 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
			}))
		{
			return;
		}
		// This scenario tests the ordinary custom-pipeline mode. The optional synthetic
		// history producer is exercised by the dedicated history/fault scenarios.
		probe.m_EnableTemporalHistory = false;
		const auto& parameters = *runtime.GetParameterControl();
		const auto& sequence = *runtime.GetFrameSequenceCoordinator();
		probe.m_ParameterControl = &parameters;
		const auto tickUntil = [&](auto condition)
			{
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
				while (!condition() && std::chrono::steady_clock::now() < deadline)
				{
					if (runtime.Tick() != AppRuntimeTickResult::Continue)
					{
						return false;
					}
					std::this_thread::yield();
				}
				return condition();
			};
		const auto ready = [&]()
			{
				const auto* frame = runtime.GetFrameCaptureCoordinator()->GetLastFrameState();
				return frame && frame->m_Readiness.IsReady();
			};
		checks.Check(tickUntil(ready), "Event sequence starts from actually ready content");
		checks.Check(runtime.GetDemoManager()->GetActiveDemo()->GetCameraRig().SetCameraPaths({ {
			.m_Id = "SEQ_Events", .m_Name = "Parameter events", .m_Version = 1,
			.m_Keys = {
				{ .m_Frame = 0, .m_Position = { 0, 1, 0 }, .m_Target = { 0, 1, 10 } },
				{ .m_Frame = 3, .m_Position = { 1, 1, 0 }, .m_Target = { 1, 1, 10 } },
			},
		} }), "Events use the production camera-path sequence");
		runtime.GetTime()->SetFixedDeltaTime(1.0 / 60.0);
		runtime.EnableParameterControl();
		// Disable inactive temporal consumers before comparing ordinary event frames.
		const std::array baselineSettings{
			LabParameterValue{ LabParameterId("view.taa.enabled"), false },
			LabParameterValue{ LabParameterId("view.gtao.temporal_accumulation"), false },
		};
		std::string baselineError;
		const auto baselineBatch = runtime.RequestParameters(AssignmentRequest(runtime, baselineSettings), baselineError);
		checks.Check(baselineBatch && tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }) &&
			runtime.TakeParameterBatchResult(baselineBatch).has_value() && tickUntil(ready),
			"The event fixture renders with supported temporal services");
		const LabParameterId exposure("view.exposure.ev100");
		const LabParameterId radius("view.gtao.radius");
		const float baseline = parameters.GetHostParameters().Get(exposure, 0.0f);
		std::string error;
		const auto event = [](uint32_t frame, LabParameterId id, LabValue value)
			{
				return FrameSequenceParameterEvent{ .m_Frame = frame, .m_Values = { { id, value } } };
			};
		for (const auto& invalid : {
			event(2, LabParameterId("unknown.event"), true),
			event(2, radius, true),
			event(2, radius, 999.0f),
			event(2, LabParameterId("deferred"), true),
			event(2, LabParameterId("layout"), uint32_t(2)),
			event(2, LabParameterId("unprojected"), 2.0f),
			event(4, exposure, baseline + 1.0f) })
		{
			const auto revision = parameters.GetParameterRevision();
			const auto applications = probe.m_ParameterApplications;
			checks.Check(runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Events",
				.m_Events = { event(0, exposure, baseline + 1.0f), invalid } }, error) != 0 &&
				tickUntil([&]() { return sequence.GetStatus()->IsTerminal(); }) &&
				sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
				parameters.GetParameterRevision() == revision &&
				parameters.GetHostParameters().Get(exposure, 0.0f) == baseline &&
				parameters.GetParameterBatchCount() == 0 && probe.m_ParameterApplications == applications,
				"A bad later event rejects the whole plan before frame zero or any parameter mutation");
		}
		FrameSequenceRequest conflict{ .m_CameraPathId = "SEQ_Events",
			.m_Events = { event(0, LabParameterId("view.taa.enabled"), false) } };
		conflict.m_Parameters = { { LabParameterId("view.taa.enabled"), true } };
		checks.Check(runtime.StartFrameSequence(conflict, error) == 0,
			"Sequence view parameters cannot silently mask an event on the same ID");
		checks.Check(runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Events",
			.m_Events = { event(0, exposure, baseline), event(0, radius, 2.0f) } }, error) == 0,
			"One sequence frame has one atomic event batch");
		const std::array retained{ LabParameterValue{ exposure, baseline } };
		const auto retainedId = runtime.RequestParameters(AssignmentRequest(runtime, retained), error);
		checks.Check(retainedId && tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }) &&
			runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Events",
				.m_Events = { event(0, exposure, baseline) } }, error) == 0 &&
			runtime.TakeParameterBatchResult(retainedId).has_value(),
			"An event sequence reserves no slot by discarding a retained external result");
		const auto revision = parameters.GetParameterRevision();
		FrameSequenceParameterEvent mixed = event(1, radius, 2.0f);
		mixed.m_Values.push_back({ LabParameterId("radius"), 2.0f });
		checks.Check(runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Events",
			.m_Events = { event(0, exposure, baseline + 1.0f), mixed,
				event(2, exposure, baseline + 1.0f), event(3, exposure, baseline + 2.0f) } }, error) != 0,
			"Immediate Host and mixed events are admitted as one plan");
		probe.m_SkipParameterFrame = true;
		GGLAB_UNUSED(runtime.Tick());
		const auto* status = sequence.GetStatus();
		const auto* aborted = parameters.GetParameterBatchStatus(
			status->m_Events[0].m_Batch->m_BatchId, ParameterBatchOwner::Sequence(status->m_SequenceId));
		checks.Check(aborted && aborted->m_Items[0].m_Published && !aborted->m_Items[0].m_FirstConsumed &&
			sequence.GetStatus()->m_SubmittedFrames == 0 && !runtime.GetExternalParameterBatchStatus() &&
			!runtime.TakeParameterBatchResult(aborted->m_BatchId),
			"An aborted frame-zero event preserves publication and excludes external result consumption");
		const auto applied = aborted ? aborted->m_Items[0].m_Applied : std::nullopt;
		GGLAB_UNUSED(runtime.Tick());
		const auto& retried = sequence.GetStatus()->m_Events[0].m_Batch;
		checks.Check(retried && retried->IsTerminal() && retried->m_Items[0].m_Applied == applied &&
			retried->m_Items[0].m_FirstConsumed > applied && parameters.GetParameterRevision() == revision + 1 &&
			sequence.GetStatus()->m_SubmittedFrames == 1 && parameters.GetParameterBatchCount() == 0,
			"Retry consumes the same event without reapplying it or allocating another revision");
		for (uint32_t frame = 1; frame < 4; ++frame)
		{
			const auto applications = probe.m_ParameterApplications;
			if (frame == 1)
			{
				probe.m_SkipParameterFrame = true;
				GGLAB_UNUSED(runtime.Tick());
			}
			GGLAB_UNUSED(runtime.Tick());
			checks.Check(sequence.GetStatus()->m_SubmittedFrames == frame + 1 &&
				probe.m_ParameterBuiltView.m_Exposure.m_ManualEV100 == baseline + (frame == 3 ? 2.0f : 1.0f) &&
				probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius == 2.0f &&
				probe.m_ParameterApplications == applications + (frame == 1 ? 1 : 0),
				std::format("Event frame {}: submitted={}, state={}, failure={}, EV={} expected={}, radius={}, applications={} expected={}",
					frame, sequence.GetStatus()->m_SubmittedFrames, GetFrameSequenceStateName(sequence.GetStatus()->m_State),
					sequence.GetStatus()->m_Failure, probe.m_ParameterBuiltView.m_Exposure.m_ManualEV100,
					baseline + (frame == 3 ? 2.0f : 1.0f), probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius,
					probe.m_ParameterApplications, applications + (frame == 1 ? 1 : 0)));
		}
		checks.Check(tickUntil([&]() { return sequence.GetStatus()->IsTerminal(); }) &&
			sequence.GetStatus()->m_State == FrameSequenceState::Completed &&
			sequence.GetStatus()->m_Events[2].m_Batch->m_Items[0].m_State == ParameterEffectState::NoChange &&
			parameters.GetParameterRevision() == revision + 3 && parameters.GetParameterBatchCount() == 0,
			"Completed event reports retain all receipts and NoChange allocates no revision");
		checks.Check(tickUntil(ready), "Ready baseline is restored before a cancellation case");
		checks.Check(runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Events",
			.m_Events = { event(0, exposure, baseline), event(2, radius, 3.0f) } }, error) != 0,
			"Cancellation sequence is admitted");
		probe.m_SkipParameterFrame = true;
		GGLAB_UNUSED(runtime.Tick());
		checks.Check(runtime.CancelFrameSequence() == FrameSequenceCancelVerdict::Cancelled &&
			parameters.GetParameterBatchCount() == 0 && sequence.GetStatus()->m_Events[0].m_Batch->IsTerminal() &&
			!sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0].m_FirstConsumed &&
			!sequence.GetStatus()->m_Events[1].m_Batch,
			"Cancel closes the owned event receipt without executing future events or inventing consumption");
		checks.Check(tickUntil(ready), "Ready baseline is restored before a content replacement case");
		checks.Check(runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Events",
			.m_Events = { event(0, exposure, baseline + 1.0f) } }, error) != 0,
			"Content-bound event sequence is admitted");
		probe.m_SkipParameterFrame = true;
		GGLAB_UNUSED(runtime.Tick());
		++probe.m_ContentSerialOffset;
		probe.m_FrameBeginUnavailable = true;
		GGLAB_UNUSED(runtime.Tick());
		checks.Check(sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
			parameters.GetParameterBatchCount() == 0 && !sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0].m_FirstConsumed,
			"Content invalidation releases the event reservation and never consumes against a replacement binding");
		runtime.Shutdown();
	}

	void RunDeferredSequenceEventScenario(SelfTestContext& checks, const std::filesystem::path& root)
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
		const auto& parameters = *runtime.GetParameterControl();
		const auto& sequence = *runtime.GetFrameSequenceCoordinator();
		const auto& environment = *probe.m_ContentServices.m_RenderServices.m_Environment;
		probe.m_ParameterControl = &parameters;
		const auto tick = [&]()
			{
				probe.m_ReadbackFence = {};
				return runtime.Tick() == AppRuntimeTickResult::Continue;
			};
		const auto until = [&](auto condition)
			{
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
				while (!condition() && std::chrono::steady_clock::now() < deadline)
				{
					if (!tick())
					{
						return false;
					}
					std::this_thread::yield();
				}
				return condition();
			};
		const auto ready = [&]()
			{
				const auto* frame = runtime.GetFrameCaptureCoordinator()->GetLastFrameState();
				return frame && frame->m_Readiness.IsReady();
			};
		checks.Check(until(ready), "Deferred events wait for an actually ready baseline");
		runtime.EnableParameterControl();
		runtime.GetTime()->SetFixedDeltaTime(1.0 / 60.0);
		const std::array baseline{
			LabParameterValue{ LabParameterId("view.taa.enabled"), false },
			LabParameterValue{ LabParameterId("view.gtao.temporal_accumulation"), false },
		};
		std::string error;
		const auto configured = runtime.RequestParameters(AssignmentRequest(runtime, baseline), error);
		checks.Check(configured && until([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }) &&
			runtime.TakeParameterBatchResult(configured).has_value() && until(ready),
			"Deferred timeline uses supported temporal services and releases external ownership");
		checks.Check(runtime.GetDemoManager()->GetActiveDemo()->GetCameraRig().SetCameraPaths({ {
			.m_Id = "SEQ_Deferred", .m_Name = "Deferred events", .m_Version = 1,
			.m_Keys = {
				{ .m_Frame = 0, .m_Position = { 0, 1, 0 }, .m_Target = { 0, 1, 10 } },
				{ .m_Frame = 63, .m_Position = { 0, 1, 0 }, .m_Target = { 0, 1, 10 } },
			},
		} }), "Deferred timeline registers one fixed 64-frame measurement range");
		const auto lighting = [](uint32_t frame, uint32_t samples)
			{
				return FrameSequenceParameterEvent{ .m_Frame = frame,
					.m_Values = { { LabParameterId("lighting"), samples } },
					.m_ExpectEffects = std::vector<std::string>{ "lighting" } };
			};
		const auto start = [&](std::vector<FrameSequenceParameterEvent> events)
			{
				return runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_Deferred",
					.m_Events = std::move(events) }, error) != 0;
			};
		for (const bool deferred : { false, true })
		{
			auto invalid = lighting(1, 2);
			if (deferred)
			{
				invalid.m_ExpectEffects = std::vector<std::string>{};
			}
			else
			{
				invalid.m_Values = { { LabParameterId("view.exposure.ev100"), 9.0f } };
			}
			const auto revision = parameters.GetParameterRevision();
			checks.Check(start({ lighting(0, 2), invalid }) && until([&]() { return sequence.GetStatus()->IsTerminal(); }) &&
				sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
				sequence.GetStatus()->m_SubmittedFrames == 0 && parameters.GetParameterRevision() == revision &&
				probe.m_LightingParameterApplications == 0,
				"A later domain expectation mismatch rejects every event before any mutation");
		}
		probe.m_HoldLightingStages = true;
		auto mixed = lighting(0, 2);
		mixed.m_Values.push_back({ LabParameterId("view.exposure.ev100"), 8.0f });
		checks.Check(start({ mixed,
			{ .m_Frame = 1, .m_Values = { { LabParameterId("radius"), 2.0f } } },
			lighting(2, 2), lighting(63, 2) }), "Deferred and Immediate events share the existing Session owner");
		probe.m_SkipParameterFrame = true;
		checks.Check(tick() && sequence.GetStatus()->m_SubmittedFrames == 0 &&
			probe.m_LightingParameterApplications == 1, "An aborted Deferred event applies exactly once");
		const auto applied = sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0].m_Applied;
		checks.Check(tick() && tick() && tick() && sequence.GetStatus()->m_SubmittedFrames == 3 &&
			sequence.GetStatus()->m_State == FrameSequenceState::Running &&
			parameters.GetParameterBatchCount() == 2,
			"Transition frames and a later Immediate batch submit while two Deferred receipts remain outstanding");
		const auto owner = ParameterBatchOwner::Sequence(sequence.GetStatus()->m_SequenceId);
		const auto generation = environment.GetBakeRequestGeneration();
		FrameCaptureGate gate{ .m_Name = "ibl", .m_State = FrameCaptureGateState::Pending,
			.m_EffectDomain = "lighting", .m_EffectGeneration = generation };
		checks.Check(parameters.CoversReadinessGate(owner, gate) &&
			!parameters.CoversReadinessGate({}, gate) &&
			!parameters.CoversReadinessGate(ParameterBatchOwner::Sequence(sequence.GetStatus()->m_SequenceId + 1), gate) &&
			!parameters.CoversReadinessGate(owner, { .m_Name = "ibl", .m_State = FrameCaptureGateState::Pending }),
			"Only this owner and its exact bound IBL target can cover a Pending gate");
		gate.m_EffectGeneration = generation + 1;
		checks.Check(!parameters.CoversReadinessGate(owner, gate),
			"A gate with different frozen generation evidence cannot borrow the live receipt");
		checks.Check(sequence.GetStatus()->m_Events[0].m_Batch->m_Items[1].m_State == ParameterEffectState::Consumed &&
			sequence.GetStatus()->m_Events[1].m_Batch->m_Items[0].m_State == ParameterEffectState::Consumed &&
			sequence.GetStatus()->m_Events[2].m_Batch->m_Items[0].m_Identity.m_Generation == generation &&
			sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0].m_Applied == applied,
			"Unrelated revisions and same-target assignments preserve independent Deferred evidence");
		bool abortedPublication = false;
		probe.m_ParameterBuiltCallback = [&]()
			{
				const auto& item = sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0];
				if (!abortedPublication && item.m_Published)
				{
					abortedPublication = true;
					probe.m_SkipParameterFrame = true;
				}
			};
		probe.m_HoldLightingStages = false;
		checks.Check(until([&]() { return abortedPublication; }) &&
			!sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0].m_FirstConsumed,
			"An aborted publication frame keeps its stamp without becoming firstConsumed");
		const auto* publicationFrame = runtime.GetFrameCaptureCoordinator()->GetLastFrameState();
		checks.Check(publicationFrame && publicationFrame->m_Readiness.IsReady() &&
			publicationFrame->m_FrameSerial ==
				sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0].m_Published &&
			publicationFrame->m_Outcome == FrameCaptureFrameOutcome::Aborted,
			"Build-time lighting publication updates readiness in that exact frame without inventing consumption");
		probe.m_ParameterBuiltCallback = {};
		checks.Check(until([&]() { return sequence.GetStatus()->IsTerminal(); }) &&
			sequence.GetStatus()->m_State == FrameSequenceState::Completed &&
			sequence.GetStatus()->m_SubmittedFrames == 64 && parameters.GetParameterBatchCount() == 0,
			"Deferred timeline completes every measurement frame and collects every internal result");
		const auto& first = sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0];
		const auto& shared = sequence.GetStatus()->m_Events[2].m_Batch->m_Items[0];
		checks.Check(first.m_State == ParameterEffectState::Consumed && first.m_Applied == applied &&
			first.m_Published > applied && first.m_FirstConsumed > first.m_Published &&
			shared.m_FirstConsumed == first.m_FirstConsumed &&
			sequence.GetStatus()->m_Events[3].m_Batch->m_Items[0].m_State == ParameterEffectState::NoChange,
			"Independent shared-target receipts consume the matching retry; final NoChange starts no transition");
		uint32_t samples = 4;
		for (const auto* pendingGate : { "environment", "asset-loads", "shaders", "ibl" })
		{
			checks.Check(until(ready) && start({ lighting(0, samples++) }) && tick(),
				"An uncovered-gate case first establishes its own pending effect");
			probe.m_ExtraPendingGate = pendingGate;
			checks.Check(tick() && sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
				parameters.GetParameterBatchCount() == 0 &&
				!sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0].m_FirstConsumed,
				"An unrelated Pending gate still fails and closes outstanding effects");
			probe.m_ExtraPendingGate.clear();
		}
		checks.Check(until(ready) && start({ lighting(0, 8) }) && tick(),
			"An external-target case establishes one pending generation");
		probe.m_ParameterEnvironment->RequestRebake();
		gate.m_EffectGeneration = environment.GetBakeRequestGeneration();
		checks.Check(!parameters.CoversReadinessGate(
			ParameterBatchOwner::Sequence(sequence.GetStatus()->m_SequenceId), gate) && tick() &&
			sequence.GetStatus()->m_State == FrameSequenceState::Failed && parameters.GetParameterBatchCount() == 0,
			"A newer unrelated Runtime target cannot borrow a sequence receipt's readiness allowance");
		checks.Check(until(ready) && start({ lighting(0, 16), lighting(1, 32) }) && tick() && tick() &&
			sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
			sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0].m_State == ParameterEffectState::Superseded &&
			parameters.GetParameterBatchCount() == 0,
			"An overlapping different assignment supersedes the first event and closes every sibling");
		checks.Check(until(ready), "The last-frame case starts from Ready");
		probe.m_HoldLightingStages = true;
		checks.Check(start({ lighting(63, 64) }) && until([&]() { return sequence.GetStatus()->IsTerminal(); }) &&
			sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
			sequence.GetStatus()->m_SubmittedFrames == 64 &&
			sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0].m_State == ParameterEffectState::EffectNotReached &&
			parameters.GetParameterBatchCount() == 0,
			"The last submitted frame closes unconsumed effects without waiting for post-range publication");
		probe.m_HoldLightingStages = false;
		checks.Check(until(ready) && start({ lighting(0, 128), lighting(1, 128), lighting(63, 256) }) && tick() && tick(),
			"Cancellation admits two independently pending records");
		checks.Check(runtime.CancelFrameSequence() == FrameSequenceCancelVerdict::Cancelled &&
			parameters.GetParameterBatchCount() == 0 &&
			sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0].m_State == ParameterEffectState::EffectNotReached &&
			sequence.GetStatus()->m_Events[1].m_Batch->m_Items[0].m_State == ParameterEffectState::EffectNotReached &&
			!sequence.GetStatus()->m_Events[2].m_Batch,
			"Cancellation closes all pending records and leaves future events unexecuted");
		checks.Check(until(ready), "A masked Immediate failure starts from Ready");
		const std::array mask{ LabParameterValue{ LabParameterId("view.gtao.radius"), 3.0f } };
		const auto maskId = runtime.RequestParameters(AssignmentRequest(runtime, mask), error);
		checks.Check(maskId && until([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }) &&
			runtime.TakeParameterBatchResult(maskId).has_value(), "The failure fixture establishes a real Host override");
		auto masked = lighting(0, 256);
		masked.m_Values.push_back({ LabParameterId("radius"), 1.0f });
		checks.Check(start({ masked }) && tick() && sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
			sequence.GetStatus()->m_Events[0].m_Batch->m_Items[1].m_State == ParameterEffectState::Superseded &&
			sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0].m_State == ParameterEffectState::EffectNotReached &&
			parameters.GetParameterBatchCount() == 0,
			"A failed Immediate sibling ends the event without waiting for its Deferred sibling");
		checks.Check(until(ready) && start({ lighting(0, 512), lighting(1, 512) }) && tick() && tick(),
			"Content invalidation starts with two pending Deferred records");
		++probe.m_ContentSerialOffset;
		probe.m_FrameBeginUnavailable = true;
		checks.Check(tick() && sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
			parameters.GetParameterBatchCount() == 0 &&
			sequence.GetStatus()->m_Events[0].m_Batch->m_Items[0].m_State == ParameterEffectState::Superseded &&
			sequence.GetStatus()->m_Events[1].m_Batch->m_Items[0].m_State == ParameterEffectState::Superseded,
			"Content replacement closes every Deferred record even while RHI BeginFrame is Unavailable");
		probe.m_FrameBeginUnavailable = false;
		runtime.Shutdown();
	}

	void RunParameterFrameScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool fatalSubmission)
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
		auto& lab = *probe.m_ParameterRuntime;
		auto& parameters = *runtime.GetParameterControl();
		probe.m_ParameterControl = &parameters;
		std::string error;
		const auto request = [&](float radius)
			{
				const std::array values{ LabParameterValue{ LabParameterId("radius"), radius } };
				return runtime.RequestParameters(AssignmentRequest(runtime, values), error);
			};
		const auto applications = probe.m_ParameterApplications;
		for (const auto& invalid : std::array{
			LabParameterValue{ LabParameterId("unknown"), true },
			LabParameterValue{ LabParameterId("radius"), int32_t(2) },
			LabParameterValue{ LabParameterId("radius"), 5.0f },
			LabParameterValue{ LabParameterId("unprojected"), 2.0f },
			LabParameterValue{ LabParameterId("unbound-rebuild"), true },
			LabParameterValue{ LabParameterId("deferred"), true } })
		{
			const std::array values{ LabParameterValue{ LabParameterId("radius"), 2.0f }, invalid };
			checks.Check(runtime.RequestParameters(AssignmentRequest(runtime, values), error) == 0 && !error.empty() &&
				probe.m_ParameterApplications == applications && !parameters.GetParameterBatchStatus(),
				"Production Lab admission rejects a complete invalid/unsupported batch without mutation or ownership");
		}
		const auto id = request(2.0f);
		checks.Check(id != 0 && error.empty() && request(3.0f) == 0 &&
			probe.m_ParameterApplications == applications &&
			lab.GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f) == 1.0f,
			"One accepted Lab transaction survives Update-time command draining without eager application");
		const std::array wrongOwnerValues{ LabParameterValue{ LabParameterId("radius"), 2.0f } };
		checks.Check(!lab.ApplyParameterBatch(id + 1, wrongOwnerValues, error),
			"A wrong source command id cannot consume the accepted session reservation");
		if (fatalSubmission)
		{
			probe.m_FrameEndInjection = FrameEndInjection::FatalFenced;
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal &&
				parameters.GetParameterBatchStatus()->m_Items.size() == 1 &&
				parameters.GetParameterBatchStatus()->m_State == ParameterBatchState::Failed &&
				!parameters.GetParameterBatchStatus()->m_Items.front().m_FirstConsumed &&
				probe.m_ParameterApplications == applications + 1,
				"A fatal fenced production frame never confirms parameter consumption");
			checks.Check(runtime.TakeParameterBatchResult(id).has_value() && request(1.0f) == 0,
				"Fatal runtime failure closes the retained receipt and stops parameter intake");
			runtime.Shutdown();
			return;
		}
		checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue &&
			parameters.GetParameterBatchStatus()->IsTerminal() && probe.m_ParameterBuiltRadius == 2.0f &&
			probe.m_ParameterApplications == applications + 1,
			"The real AppRuntime Ready/build/submit path applies the Lab batch to the built frame exactly once");
		const auto* status = parameters.GetParameterBatchStatus();
		const auto revision = status->m_Revision;
		const auto& item = status->m_Items.front();
		checks.Check(item.m_State == ParameterEffectState::Consumed &&
			item.m_Requested == probe.m_ParameterBuiltSerial && item.m_Applied == item.m_Requested &&
			item.m_Published == item.m_Applied && item.m_FirstConsumed == item.m_Applied &&
			request(3.0f) == 0 && !runtime.TakeParameterBatchResult(id + 1),
			"Four Immediate stamps bind to the actual submitted serial and the owner retains its result until consumption");
		checks.Check(runtime.TakeParameterBatchResult(id).has_value() && !runtime.TakeParameterBatchResult(id),
			"The bounded Lab result has exactly one explicit consumption");

		const auto noChange = request(2.0f);
		GGLAB_UNUSED(runtime.Tick());
		const auto unchanged = runtime.TakeParameterBatchResult(noChange);
		checks.Check(unchanged && unchanged->m_Revision == revision &&
			unchanged->m_Items.front().m_State == ParameterEffectState::NoChange &&
			!unchanged->m_Items.front().m_Published && probe.m_ParameterApplications == applications + 1,
			"NoChange preserves the active revision and starts neither a handler call nor publication");

		const auto retry = request(3.0f);
		probe.m_SkipParameterFrame = true;
		const auto aborts = probe.m_FrameAborts;
		GGLAB_UNUSED(runtime.Tick());
		const auto published = parameters.GetParameterBatchStatus()->m_Items.front().m_Published;
		checks.Check(published && !parameters.GetParameterBatchStatus()->IsTerminal() &&
			!parameters.GetParameterBatchStatus()->m_Items.front().m_FirstConsumed &&
			probe.m_FrameAborts == aborts + 1 && probe.m_ParameterApplications == applications + 2,
			"A real RAII frame abort preserves publication without firstConsumed");
		GGLAB_UNUSED(runtime.Tick());
		const auto retried = runtime.TakeParameterBatchResult(retry);
		checks.Check(retried && retried->m_Items.front().m_Published == published &&
			retried->m_Items.front().m_FirstConsumed == *published + 1 &&
			probe.m_ParameterBuiltRadius == 3.0f && probe.m_ParameterApplications == applications + 2,
			"Only a matching successful retry consumes the state; application and publication are not repeated");

		const auto superseded = request(2.5f);
		probe.m_SkipParameterFrame = true;
		GGLAB_UNUSED(runtime.Tick());
		const auto oldPublication = parameters.GetParameterBatchStatus()->m_Items.front().m_Published;
		lab.RequestSetParameter(LabParameterId("radius"), 1.5f);
		GGLAB_UNUSED(runtime.Tick());
		const auto replaced = runtime.TakeParameterBatchResult(superseded);
		checks.Check(replaced && replaced->m_Items.front().m_State == ParameterEffectState::Superseded &&
			replaced->m_Items.front().m_Published == oldPublication &&
			!replaced->m_Items.front().m_FirstConsumed && probe.m_ParameterBuiltRadius == 1.5f,
			"A later UI edit supersedes the aborted state instead of being mislabeled as its consumption");

		const auto frozen = request(2.0f);
		probe.m_ChangeParameterAfterBuild = true;
		GGLAB_UNUSED(runtime.Tick());
		const auto consumed = runtime.TakeParameterBatchResult(frozen);
		checks.Check(consumed && consumed->m_Items.front().m_State == ParameterEffectState::Consumed &&
			consumed->m_Items.front().m_FirstConsumed == probe.m_ParameterBuiltSerial &&
			probe.m_ParameterBuiltRadius == 2.0f &&
			lab.GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f) == 3.5f,
			"Submission uses the identity frozen by scene build rather than re-reading a later live value");

		const auto unrelated = request(2.5f);
		probe.m_UnrelatedLightingChange = true;
		GGLAB_UNUSED(runtime.Tick());
		const auto uncertain = runtime.TakeParameterBatchResult(unrelated);
		checks.Check(uncertain && uncertain->m_State == ParameterBatchState::Failed &&
			uncertain->m_Failure.find("UnexpectedLightingTransition") != std::string::npos &&
			!uncertain->m_Items.front().m_FirstConsumed,
			"An unrelated lighting transition fails conservatively without inventing parameter attribution");

		const std::array rebake{ LabParameterValue{ LabParameterId("rebake"), true } };
		const auto mismatched = runtime.RequestParameters(AssignmentRequest(runtime, rebake), error);
		GGLAB_UNUSED(runtime.Tick());
		const auto mismatch = runtime.TakeParameterBatchResult(mismatched);
		checks.Check(mismatch && mismatch->m_State == ParameterBatchState::Failed &&
			mismatch->m_Failure.find("EffectMismatch") != std::string::npos &&
			!mismatch->m_Items.front().m_FirstConsumed,
			"An actual Runtime lighting generation advance cannot masquerade as an Immediate effect");

		const auto queued = request(1.0f);
		lab.RequestRestartSession();
		GGLAB_UNUSED(runtime.Tick());
		const auto stale = runtime.TakeParameterBatchResult(queued);
		checks.Check(stale && stale->m_State == ParameterBatchState::Failed && stale->m_Items.empty(),
			"Replacing the Lab session rejects a queued batch before it can mutate the replacement");
		runtime.Shutdown();
	}
}

namespace gglab
{
	using namespace runtime_test;

	void RunSequenceParameterIntegrationSelfTests(SelfTestContext& context) noexcept
	{
		RuntimeTestDirectory directory("sequence");
		const auto& root = directory.GetPath();
		RunSequenceParameterEventScenario(context, root / "sequence-events");
		RunDeferredSequenceEventScenario(context, root / "deferred-sequence-events");
		RunParameterFrameScenario(context, root / "parameter-frames", false);
		RunParameterFrameScenario(context, root / "parameter-failed-submit", true);
	}
}
