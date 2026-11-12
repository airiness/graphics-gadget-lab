#include "ApplicationInput.h"
#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "Capture/CapturePublisher.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "Demo/DemoManager.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Time.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabTestCore/SelfTest.h"
#include "GpuFaultLifecycleSelfTests.h"
#include "Lab/LabParameter.h"
#include "RuntimeIntegrationTestCases.h"
#include "RuntimeIntegrationTestServices.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gglab::runtime_test
{
	void RunRequiredPlanOutcomeScenario(SelfTestContext& checks, const std::filesystem::path& root,
		uint32_t referenceSamples, bool missingResource)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		// Encoding is a stand-in here; native backend captures verify real PNGs.
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root,
			[&](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
			{
				++probe.m_ImageEncodes;
				return std::vector<uint8_t>{ 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
			}))
		{
			return;
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!runtime.GetFrameCaptureCoordinator()->GetLastFrameState()->m_Readiness.IsReady() &&
			std::chrono::steady_clock::now() < deadline)
		{
			GGLAB_UNUSED(runtime.Tick());
			std::this_thread::yield();
		}
		auto& rig = runtime.GetDemoManager()->GetActiveDemo()->GetCameraRig();
		checks.Check(rig.SetCameraPaths({ {
			.m_Id = "SEQ_RequiredProducts", .m_Name = "Required products", .m_Version = 1,
			.m_Keys = {
				{ .m_Frame = 0, .m_Position = { 0, 1, 0 }, .m_Target = { 0, 1, 10 } },
				{ .m_Frame = 1, .m_Position = { 1, 1, 0 }, .m_Target = { 1, 1, 10 } },
			},
		} }), "The production camera rig registers a deterministic required-product sequence");
		runtime.GetTime()->SetFixedDeltaTime(1.0 / 60.0);
		auto& sequence = *runtime.GetFrameSequenceCoordinator();
		auto& capture = *runtime.GetFrameCaptureCoordinator();
		std::string error;
		checks.Check(runtime.StartFrameSequence({ .m_CameraPathId = "SEQ_RequiredProducts", .m_CaptureFrames = { 0 },
			.m_Exports = { { .m_Frame = 0,
				.m_Targets = { { .m_Resource = missingResource ? "Missing.RequiredResource" : "FaultLifecycle.Color" } },
				.m_IncludeGraph = true } },
			.m_ReferenceSamples = referenceSamples,
			.m_Events = referenceSamples == 0 ? std::vector<FrameSequenceParameterEvent>{ {
				.m_Frame = 0, .m_Values = { { LabParameterId("view.exposure.ev100"), 3.0f } },
			} } : std::vector<FrameSequenceParameterEvent>{} }, error) != 0,
			"The production sequence accepts image, arrays and graph as one required-product plan");
		if (missingResource)
		{
			const auto executions = probe.m_GraphExecutions;
			const auto ends = probe.m_FrameEnds;
			const auto aborts = probe.m_FrameAborts;
			const auto maps = probe.m_ReadbackMaps;
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue &&
				probe.m_GraphExecutions == executions && probe.m_FrameEnds == ends &&
				probe.m_FrameAborts == aborts + 1 && probe.m_ReadbackMaps == maps,
				"A permanent compiled-resource failure aborts before image recording, readback or partial submission");
			checks.Check(sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
				sequence.GetStatus()->m_SubmittedFrames == 0 && runtime.GetCaptureIntakeSnapshot().m_AppRuntimePublishedResults == 0 &&
				sequence.GetStatus()->m_AcceptedCaptureJobs == 1 && sequence.GetStatus()->m_FinishedCaptureJobs == 1 &&
				sequence.GetStatus()->m_Failure.find("UnknownResource") != std::string::npos &&
				runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0 && capture.GetUnfinishedRequestCount() == 0 &&
				probe.m_CaptureControl->GetUnfinishedRequestCount() == 0 && probe.m_ImageEncodes == 0,
				"The permanent failure consumes one sequence receipt without a legacy fan-out, encoding or leaked reservation");
		}
		else
		{
			probe.m_RequiredPlanDeferrals = 2;
			for (uint32_t attempt = 0; attempt < 8 && probe.m_RequiredPlanDeferrals == 2; ++attempt)
			{
				GGLAB_UNUSED(runtime.Tick());
			}
			const double time = runtime.GetTime()->GetTotalTime();
			const auto serial = capture.GetLastFrameState()->m_FrameSerial;
			const auto accepted = sequence.GetStatus()->m_AcceptedCaptureJobs;
			const auto executions = probe.m_GraphExecutions;
			const auto ends = probe.m_FrameEnds;
			const auto aborts = probe.m_FrameAborts;
			const auto maps = probe.m_ReadbackMaps;
			checks.Check(probe.m_RequiredPlanDeferrals == 1 && accepted == 1 &&
				sequence.GetStatus()->m_State == FrameSequenceState::Running &&
				sequence.GetStatus()->m_SubmittedFrames == 0 && capture.GetUnfinishedRequestCount() == 2 &&
				sequence.ShouldHoldTime() && probe.m_CaptureControl->GetUnfinishedRequestCount() == 0,
				"Late production deferral retires bound Runtime owners but preserves "
				"the accepted job and unsubmitted sequence sample");
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue && probe.m_RequiredPlanDeferrals == 0 &&
				probe.m_GraphExecutions == executions && probe.m_FrameEnds == ends && probe.m_FrameAborts == aborts + 1 &&
				probe.m_ReadbackMaps == maps && runtime.GetTime()->GetTotalTime() == time &&
				capture.GetLastFrameState()->m_FrameSerial == serial + 1 &&
				sequence.GetStatus()->m_AcceptedCaptureJobs == accepted && sequence.GetStatus()->m_SubmittedFrames == 0 &&
				capture.GetJobSnapshot(1).has_value(),
				"A second real RAII abort records no GPU work, maps no payload "
				"and repeats the same simulation instant with fresh provenance");
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue && probe.m_FrameEnds == ends + 1 &&
				probe.m_GraphExecutions == executions + 1 && runtime.GetTime()->GetTotalTime() == time &&
				sequence.GetStatus()->m_SubmittedFrames == 1,
				"Only the successful complete-plan retry submits and advances the sequence "
				"at the preserved simulation instant");
			if (referenceSamples == 0)
			{
				const auto& event = sequence.GetStatus()->m_Events[0].m_Batch;
				checks.Check(event && event->IsTerminal() && event->m_Items[0].m_FirstConsumed ==
					capture.GetLastFrameState()->m_FrameSerial && event->m_Items[0].m_Applied < event->m_Items[0].m_FirstConsumed &&
					runtime.GetParameterControl()->GetParameterBatchCount() == 0,
					"Repeated real capture-admission aborts preserve one event and only the successful retry consumes it");
			}
			const auto successfulSerial = capture.GetLastFrameState()->m_FrameSerial;
			for (uint32_t attempt = 0; attempt < 100 && sequence.IsActive(); ++attempt)
			{
				GGLAB_UNUSED(runtime.Tick());
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			std::vector<nlohmann::json> manifests;
			if (std::filesystem::exists(root / "Captures"))
			{
				for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "Captures"))
				{
					if (entry.path().filename().string().ends_with(".capture-bundle.json"))
					{
						std::ifstream file(entry.path());
						manifests.push_back(nlohmann::json::parse(file));
					}
				}
			}
			const auto frameManifest = std::ranges::find_if(manifests,
				[](const auto& manifest) { return manifest.at("consistency") == "same-frame"; });
			const auto rangeManifest = std::ranges::find_if(manifests,
				[](const auto& manifest) { return manifest.at("consistency") == "sequence-range"; });
			checks.Check(runtime.GetCaptureIntakeSnapshot().m_AppRuntimePublishedResults == 0 && manifests.size() == 2 &&
				frameManifest != manifests.end() && rangeManifest != manifests.end() &&
				(*frameManifest)["products"].size() == 3 && (*rangeManifest)["products"].size() == 1 &&
				(*frameManifest)["provenance"]["frame"]["serial"] == successfulSerial &&
				(*frameManifest)["provenance"]["parameters"]["frameSerial"] == successfulSerial &&
				(*frameManifest)["provenance"]["time"]["totalTime"] == time &&
				(*frameManifest)["provenance"]["sequence"]["frame"] == 0 &&
				sequence.GetStatus()->m_State == FrameSequenceState::Completed &&
				sequence.GetStatus()->m_ParameterPublication->m_Outcome == CapturePublicationOutcome::Published &&
				sequence.GetStatus()->m_CompletedCaptures == 2 && sequence.GetStatus()->m_FinishedCaptureJobs == 1 &&
				runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0 &&
				probe.m_ImageEncodes == 1 && capture.GetUnfinishedRequestCount() == 0 &&
				probe.m_CaptureControl->GetUnfinishedRequestCount() == 0 &&
				probe.m_CaptureControl->GetResourceExportFrameAvailability() == FrameCaptureAvailability::Available,
				"Production image, graph and NPY publication uses only the successful attempt "
				"and releases all payload/write reservations");
		}
		runtime.Shutdown();
		checks.Check(runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0 && probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0,
			"Required-product teardown adds no duplicate terminal result and releases GPU owners only after safe settlement");
	}

	void RunOwnedJobRoutingScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		const auto waitingPlan = []()
			{
				return CapturePlan{ .m_RequiredProducts = { ImageCaptureProduct{} },
					.m_Timing = FrameCaptureTiming::AfterReady,
					.m_RequiredContentId = "Missing.RoutingContent" };
			};
		auto plan = waitingPlan();
		const auto unavailable = runtime.TrySubmitCaptureJob(std::move(plan));
		checks.Check(unavailable.m_Status == CaptureAdmissionStatus::Rejected &&
			unavailable.m_Reason == CaptureAdmissionReason::ServicesUnavailable && unavailable.m_JobId == 0 &&
			plan.m_RequiredProducts.size() == 1 && !runtime.FindCaptureJobResult(0) &&
			!runtime.TakeCaptureJobResult(1) && !runtime.GetCaptureJobSnapshot(1) &&
			runtime.CancelCaptureJob(1) == CaptureCancelResult::NotFound &&
			runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0,
			"Uncomposed AppRuntime rejects job intake without consuming plans or manufacturing receipts");
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root, {},
			{ .m_MaxActiveJobs = 1, .m_MaxReceipts = 2 }))
		{
			return;
		}
		auto& capture = *runtime.GetFrameCaptureCoordinator();
		const auto first = runtime.TrySubmitCaptureJob(std::move(plan));
		plan = waitingPlan();
		const auto busy = runtime.TrySubmitCaptureJob(std::move(plan));
		checks.Check(first.m_Status == CaptureAdmissionStatus::Accepted && first.m_JobId == 1 &&
			busy.m_Status == CaptureAdmissionStatus::Busy && busy.m_Reason == CaptureAdmissionReason::ActiveJobLimit &&
			plan.m_RequiredProducts.size() == 1 && runtime.GetCaptureIntakeSnapshot().m_Limits.m_MaxReceipts == 2 &&
			runtime.GetCaptureJobSnapshot(first.m_JobId)->m_Stage == CaptureJobStage::Waiting,
			"AppRuntime applies its configured shared active and receipt limits before moving a caller's plan");
		checks.Check(runtime.CancelCaptureJob(first.m_JobId) == CaptureCancelResult::Cancelled &&
			!capture.TakeJobResult(first.m_JobId) && !runtime.TakeCaptureJobResult(first.m_JobId) &&
			!runtime.FindCaptureJobResult(first.m_JobId) &&
			runtime.GetCaptureIntakeSnapshot().m_AppRuntimeUnpublishedResults == 1,
			"A terminal receipt cannot escape through the coordinator or host before AppRuntime routes it");
		const auto second = runtime.TrySubmitCaptureJob(std::move(plan));
		checks.Check(second.m_JobId == 2 && runtime.CancelCaptureJob(second.m_JobId) == CaptureCancelResult::Cancelled,
			"Terminal completion returns active credit without releasing the reserved host receipt");
		capture.Update();
		checks.Check(!runtime.FindCaptureJobResult(first.m_JobId) && !runtime.FindCaptureJobResult(second.m_JobId),
			"Updating transport directly does not bypass AppRuntime's host-publication boundary");
		plan = waitingPlan();
		bool bounded = true;
		for (uint32_t attempt = 0; attempt < 64; ++attempt)
		{
			const auto full = runtime.TrySubmitCaptureJob(std::move(plan));
			bounded &= full.m_Status == CaptureAdmissionStatus::Busy && full.m_JobId == 0 &&
				full.m_Reason == CaptureAdmissionReason::ReceiptLimit;
		}
		checks.Check(bounded && plan.m_RequiredProducts.size() == 1 &&
			runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 2,
			"Unconsumed terminal receipts bound repeated intake even before the host polls");
		GGLAB_UNUSED(runtime.Tick());
		const CaptureJobResult* borrowed = runtime.FindCaptureJobResult(first.m_JobId);
		const auto published = runtime.GetCaptureIntakeSnapshot();
		checks.Check(borrowed && borrowed->m_Status == CaptureJobStatus::Cancelled &&
			borrowed->m_Publication == CaptureJobPublication::NotPublished &&
			runtime.FindCaptureJobResult(first.m_JobId) == borrowed && runtime.FindCaptureJobResult(second.m_JobId) &&
			published.m_ActiveJobs == 0 && published.m_ReservedReceipts == 2 && published.m_TerminalReceipts == 2 &&
			published.m_AppRuntimeUnpublishedResults == 0 && published.m_AppRuntimePublishedResults == 2,
			"Tick publishes immutable receipts to multiple non-consuming observers "
			"without moving or releasing their slots");
		checks.Check(!capture.TakeJobResult(first.m_JobId) && !runtime.TakeCaptureJobResult(0) &&
			!runtime.TakeCaptureJobResult(999) &&
			capture.TrySubmit(std::move(plan)).m_Reason == CaptureAdmissionReason::ReceiptLimit &&
			runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 2,
			"Neither the standalone drain nor direct coordinator admission bypasses retained host capacity");
		const auto consumed = runtime.TakeCaptureJobResult(second.m_JobId);
		const auto third = runtime.TrySubmitCaptureJob(std::move(plan));
		checks.Check(consumed && consumed->m_JobId == second.m_JobId && third.m_JobId == 3 &&
			!runtime.TakeCaptureJobResult(second.m_JobId) && !runtime.GetCaptureJobSnapshot(second.m_JobId) &&
			runtime.FindCaptureJobResult(first.m_JobId) == borrowed &&
			runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 2,
			"Out-of-order consumption returns exactly one slot; reuse preserves another observer's borrowed receipt");
		runtime.HandleHostEvent({ .m_Type = AppHostEventType::ExitRequested });
		plan = waitingPlan();
		const auto exiting = runtime.TrySubmitCaptureJob(std::move(plan));
		checks.Check(exiting.m_Status == CaptureAdmissionStatus::Rejected &&
			exiting.m_Reason == CaptureAdmissionReason::ShuttingDown && plan.m_RequiredProducts.size() == 1,
			"An exiting AppRuntime stops unified intake before shutdown without consuming the plan");
		runtime.Shutdown();
		runtime.Shutdown();
		const auto stopped = runtime.GetCaptureIntakeSnapshot();
		checks.Check(probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0 &&
			runtime.FindCaptureJobResult(first.m_JobId) == borrowed && runtime.FindCaptureJobResult(third.m_JobId) &&
			stopped.m_ActiveJobs == 0 && stopped.m_ReservedReceipts == 2 &&
			stopped.m_AppRuntimePublishedResults == 2 &&
			runtime.CancelCaptureJob(first.m_JobId) == CaptureCancelResult::Finished,
			"Repeated shutdown retains and publishes each reserved receipt after device teardown, "
			"with every receipt retaining its original bounded slot");
		const auto firstResult = runtime.TakeCaptureJobResult(first.m_JobId);
		const auto thirdResult = runtime.TakeCaptureJobResult(third.m_JobId);
		checks.Check(firstResult && thirdResult && thirdResult->m_Status == CaptureJobStatus::Cancelled &&
			!runtime.TakeCaptureJobResult(third.m_JobId) && !runtime.FindCaptureJobResult(first.m_JobId) &&
			runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0 &&
			runtime.TrySubmitCaptureJob(std::move(plan)).m_Reason == CaptureAdmissionReason::ShuttingDown,
			"The host can consume final results exactly once after shutdown without reopening intake");
	}

	void RunOwnedJobFailureScenario(SelfTestContext& checks, const std::filesystem::path& root,
		FrameEndInjection injection, bool completionUnknown)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		const auto admitted = runtime.TrySubmitCaptureJob({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		const auto maps = probe.m_ReadbackMaps;
		probe.m_FrameEndInjection = injection;
		probe.m_Inject = completionUnknown;
		checks.Check(admitted.m_Status == CaptureAdmissionStatus::Accepted &&
			runtime.Tick() == AppRuntimeTickResult::Fatal && probe.m_ReadbackMaps == maps,
			"Submission failure and unknown completion fail the production host "
			"without mapping a job's GPU data");
		runtime.Shutdown();
		const auto* borrowed = runtime.FindCaptureJobResult(admitted.m_JobId);
		checks.Check(borrowed && borrowed->m_Status == CaptureJobStatus::Failed &&
			borrowed->m_Publication == CaptureJobPublication::NotPublished && borrowed->m_ManifestPath.empty() &&
			runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 1 &&
			runtime.GetCaptureIntakeSnapshot().m_AppRuntimePublishedResults == 1 &&
			probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0 && probe.m_LiveReadbackBuffers == 0,
			"Fatal shutdown publishes one reserved failure receipt only through "
			"its AppRuntime owner after safe GPU retirement");
		const auto result = runtime.TakeCaptureJobResult(admitted.m_JobId);
		runtime.Shutdown();
		checks.Check(result && result->m_Products.size() == 1 && !result->m_Products.front().m_Failure.empty() &&
			!runtime.TakeCaptureJobResult(admitted.m_JobId) && runtime.GetCaptureIntakeSnapshot().m_AppRuntimePublishedResults == 0 &&
			runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0 &&
			runtime.GetLifecycleState() == AppRuntimeLifecycleState::Failed,
			"Post-shutdown host consumption returns failure credit once "
			"and leaves no published receipt after consumption");
	}

	void RunOwnedWriterDrainScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		std::atomic<bool> entered = false;
		std::atomic<bool> release = false;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root,
			[&](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
			{
				++probe.m_ImageEncodes;
				entered = true;
				while (!release)
				{
					std::this_thread::yield();
				}
				return std::vector<uint8_t>{ 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
			}, { .m_MaxActiveJobs = 1, .m_MaxReceipts = 1 }))
		{
			return;
		}
		const auto admitted = runtime.TrySubmitCaptureJob({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!entered && std::chrono::steady_clock::now() < deadline)
		{
			GGLAB_UNUSED(runtime.Tick());
			std::this_thread::yield();
		}
		checks.Check(entered && runtime.CancelCaptureJob(admitted.m_JobId) == CaptureCancelResult::Cancelled,
			"The AppRuntime cancellation API withdraws delivery "
			"while the real writer is blocked inside its codec");
		GGLAB_UNUSED(runtime.Tick());
		const auto result = runtime.TakeCaptureJobResult(admitted.m_JobId);
		const auto draining = runtime.GetCaptureIntakeSnapshot();
		checks.Check(result && result->m_Status == CaptureJobStatus::Cancelled &&
			result->m_Publication == CaptureJobPublication::NotPublished && draining.m_ReservedReceipts == 0 &&
			draining.m_DrainingWriterJobs == 1 && draining.m_DrainingWriteReservations == 1,
			"Host acknowledgement releases only the receipt; "
			"a cancelled codec retains its independent writer reservation");
		const auto replacement = runtime.TrySubmitCaptureJob({ .m_RequiredProducts = { ImageCaptureProduct{} },
			.m_Timing = FrameCaptureTiming::AfterReady, .m_RequiredContentId = "Missing.WriterContent" });
		checks.Check(replacement.m_Status == CaptureAdmissionStatus::Accepted &&
			runtime.GetCaptureIntakeSnapshot().m_DrainingWriteReservations == 1,
			"Reusing an acknowledged receipt slot neither loses nor duplicates "
			"the cancelled writer's drain ownership");
		release = true;
		const auto drainDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (runtime.GetCaptureIntakeSnapshot().m_DrainingWriterJobs != 0 &&
			std::chrono::steady_clock::now() < drainDeadline)
		{
			GGLAB_UNUSED(runtime.Tick());
			std::this_thread::yield();
		}
		runtime.Shutdown();
		const auto replacementResult = runtime.TakeCaptureJobResult(replacement.m_JobId);
		bool manifest = false;
		if (std::filesystem::exists(root / "Captures"))
		{
			for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "Captures"))
			{
				manifest |= entry.path().filename().string().ends_with(".capture-bundle.json");
			}
		}
		checks.Check(replacementResult && replacementResult->m_Status == CaptureJobStatus::Cancelled &&
			!runtime.TakeCaptureJobResult(admitted.m_JobId) && !manifest &&
			runtime.GetCaptureIntakeSnapshot().m_DrainingWriterJobs == 0 &&
			runtime.GetCaptureIntakeSnapshot().m_DrainingWriteReservations == 0 &&
			runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0 && probe.m_EarlyDestructions == 0,
			"Late codec completion retires its writer without publishing a manifest "
			"or emitting another host result");
	}

	void RunAdmittedJobScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool missingResource, bool multipleImages)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root,
			[&](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
			{
				++probe.m_ImageEncodes;
				return std::vector<uint8_t>{ 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
			}))
		{
			return;
		}
		auto& capture = *runtime.GetFrameCaptureCoordinator();
		CapturePlan plan{ .m_RequiredProducts = {
			ImageCaptureProduct{}, ResourceCaptureProduct{ { {
				.m_Resource = missingResource ? "Missing.AdmittedResource" : "FaultLifecycle.Color" } } },
			GraphCaptureProduct{} } };
		if (multipleImages)
		{
			plan.m_RequiredProducts.push_back(ImageCaptureProduct{ CaptureImageSource::Composited });
		}
		const auto admission = runtime.TrySubmitCaptureJob(std::move(plan));
		checks.Check(admission.m_Status == CaptureAdmissionStatus::Accepted && admission.m_JobId != 0,
			"The production AppRuntime accepts one logical job for image, resource and graph products");
		const auto maps = probe.m_ReadbackMaps;
		const auto aborts = probe.m_FrameAborts;
		if (!missingResource)
		{
			probe.m_RequiredPlanDeferrals = 2;
			GGLAB_UNUSED(runtime.Tick());
			GGLAB_UNUSED(runtime.Tick());
			const auto snapshot = runtime.GetCaptureJobSnapshot(admission.m_JobId);
			checks.Check(snapshot && snapshot->m_AttemptCount == 2 &&
				snapshot->m_Stage == CaptureJobStage::Waiting && probe.m_FrameAborts == aborts + 2 &&
				probe.m_ReadbackMaps == maps && !runtime.TakeCaptureJobResult(admission.m_JobId) &&
				capture.GetIntakeSnapshot().m_ReservedReceipts == 1,
				"Production late deferral safely aborts twice while retaining one accepted job and receipt");
		}
		std::optional<CaptureJobResult> result;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!result && std::chrono::steady_clock::now() < deadline)
		{
			GGLAB_UNUSED(runtime.Tick());
			result = runtime.TakeCaptureJobResult(admission.m_JobId);
			std::this_thread::yield();
		}
		checks.Check(result && result->m_Products.size() == (multipleImages ? 4u : 3u) &&
			result->m_Status == (missingResource ? CaptureJobStatus::Failed : CaptureJobStatus::Completed) &&
			result->m_Publication == (missingResource ? CaptureJobPublication::NotPublished : CaptureJobPublication::Published) &&
			result->m_ManifestPath.empty() == missingResource,
			std::format("Production compiled failure or successful GPU transport reaches exactly one logical job result ({}).",
				result && !result->m_Products.empty() ? result->m_Products.front().m_Failure : "no receipt"));
		if (result && result->m_Products.front().m_Metadata)
		{
			const auto serial = result->m_Products.front().m_Metadata->m_FrameSerial;
			checks.Check(std::ranges::all_of(result->m_Products, [&](const auto& product)
				{
					return product.m_Metadata && product.m_Metadata->m_FrameSerial == serial;
				}) && probe.m_ImageEncodes == (missingResource ? 0u : multipleImages ? 2u : 1u) &&
				(!missingResource || (probe.m_ReadbackMaps == maps && probe.m_FrameAborts == aborts + 1)),
				"Job product provenance comes from one actual attempt; compiled failure maps and encodes nothing");
		}
		if (multipleImages)
		{
			checks.Check(result && result->m_Products.size() == 4 &&
				result->m_Products[0].m_Metadata && result->m_Products[3].m_Metadata &&
				result->m_Products[0].m_Metadata->m_Source == FrameCaptureSource::Scene &&
				result->m_Products[3].m_Metadata->m_Source == FrameCaptureSource::Composited &&
				result->m_Products[0].m_Files != result->m_Products[3].m_Files,
				"Distinct same-frame image sources compile separate graph imports and publish separate product files");
		}
		checks.Check(runtime.GetCaptureIntakeSnapshot().m_AppRuntimePublishedResults == 0 && capture.GetIntakeSnapshot().m_ReservedReceipts == 0 &&
			capture.GetUnfinishedRequestCount() == 0 && probe.m_CaptureControl->GetUnfinishedRequestCount() == 0,
			"Production receipt consumption releases all host and transport reservations");
		GGLAB_UNUSED(runtime.Tick());
		checks.Check(!runtime.TakeCaptureJobResult(admission.m_JobId), "A later production tick cannot repeat the terminal receipt");
		runtime.Shutdown();
		checks.Check(probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0,
			"Admitted-job teardown preserves the production GPU ownership settlement order");
	}

	void RunJobCancellationScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool beforeSubmit)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root,
			[&](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
			{
				++probe.m_ImageEncodes;
				return std::vector<uint8_t>{ 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
			}))
		{
			return;
		}
		auto& capture = *runtime.GetFrameCaptureCoordinator();
		probe.m_AppRuntime = &runtime;
		const auto cancelled = runtime.TrySubmitCaptureJob(beforeSubmit ?
			CapturePlan{ .m_RequiredProducts = { ImageCaptureProduct{},
				ResourceCaptureProduct{ { { .m_Resource = "FaultLifecycle.Color" } } }, GraphCaptureProduct{} } } :
			CapturePlan{ .m_RequiredProducts = { ImageCaptureProduct{} } });
		checks.Check(cancelled.m_Status == CaptureAdmissionStatus::Accepted,
			"The production cancellation harness accepts its logical job");
		const auto maps = probe.m_ReadbackMaps;
		const auto aborts = probe.m_FrameAborts;
		const auto ends = probe.m_FrameEnds;
		const auto copies = probe.m_CaptureCopies;
		std::optional<CaptureJobResult> cancelledResult;
		if (beforeSubmit)
		{
			probe.m_CancelJobDuringBuild = cancelled.m_JobId;
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue &&
				probe.m_CancellationResult == CaptureCancelResult::Cancelled &&
				probe.m_FrameAborts == aborts + 1 && probe.m_FrameEnds == ends &&
				probe.m_CaptureCopies == copies && probe.m_ReadbackMaps == maps,
				"Cancelling after image binding aborts through real frame RAII before graph execution, mapping or submission");
			capture.Update();
			checks.Check(capture.GetIntakeSnapshot().m_DrainingRuntimeRequests == 0 &&
				probe.m_CaptureControl->GetUnfinishedRequestCount() == 0 && probe.m_ImageEncodes == 0,
				"The real abort drains provisional cancelled capture owners without encoding a partial bundle");
		}
		else
		{
			const auto surviving = runtime.TrySubmitCaptureJob({ .m_RequiredProducts = { ImageCaptureProduct{} } });
			probe.m_HoldCaptureFence = true;
			GGLAB_UNUSED(runtime.Tick());
			const auto buffers = probe.m_LiveReadbackBuffers;
			checks.Check(capture.GetJobSnapshot(cancelled.m_JobId)->m_Stage == CaptureJobStage::AwaitingProducts &&
				probe.m_FrameEnds == ends + 1 && probe.m_CaptureCopies == copies + 1 && buffers != 0 &&
				probe.m_ReadbackMaps == maps && probe.m_CaptureControl->GetUnfinishedRequestCount() == 2,
				"Two production subscribers share one image copy while its certified submission remains pending");
			checks.Check(runtime.CancelCaptureJob(cancelled.m_JobId) == CaptureCancelResult::Cancelled,
				"The submitted production job accepts delivery cancellation");
			capture.Update();
			checks.Check(probe.m_LiveReadbackBuffers == buffers && probe.m_ReadbackMaps == maps &&
				capture.GetIntakeSnapshot().m_DrainingRuntimeRequests == 1 &&
				!runtime.TakeCaptureJobResult(surviving.m_JobId),
				"Cancelling one subscriber neither maps nor retires its pending shared GPU buffer");
			GGLAB_UNUSED(runtime.Tick());
			cancelledResult = runtime.TakeCaptureJobResult(cancelled.m_JobId);
			checks.Check(cancelledResult && runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 1 &&
				runtime.GetCaptureIntakeSnapshot().m_DrainingRuntimeRequests == 1 &&
				probe.m_LiveReadbackBuffers == buffers && probe.m_ReadbackMaps == maps,
				"Host receipt consumption returns no GPU or draining reservation while the shared submission is pending");
			probe.m_HoldCaptureFence = false;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			std::optional<CaptureJobResult> result;
			while (!result && std::chrono::steady_clock::now() < deadline)
			{
				GGLAB_UNUSED(runtime.Tick());
				result = runtime.TakeCaptureJobResult(surviving.m_JobId);
				std::this_thread::yield();
			}
			checks.Check(result && result->m_Status == CaptureJobStatus::Completed &&
				std::filesystem::exists(result->m_ManifestPath) && probe.m_ImageEncodes == 1 &&
				capture.GetIntakeSnapshot().m_DrainingRuntimeRequests == 0,
				"Certified completion delivers and publishes only the surviving subscriber and returns cancelled drain credit");
		}
		if (!cancelledResult)
		{
			cancelledResult = runtime.TakeCaptureJobResult(cancelled.m_JobId);
		}
		checks.Check(cancelledResult && cancelledResult->m_Status == CaptureJobStatus::Cancelled &&
			cancelledResult->m_Publication == CaptureJobPublication::NotPublished && cancelledResult->m_ManifestPath.empty(),
			"Production draining preserves exactly the original cancelled, unpublished job receipt");
		runtime.Shutdown();
		checks.Check(runtime.GetCaptureIntakeSnapshot().m_AppRuntimePublishedResults == 0 && !runtime.TakeCaptureJobResult(cancelled.m_JobId) &&
			probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0 && probe.m_LiveReadbackBuffers == 0,
			"Cancellation shutdown preserves GPU ownership and emits no duplicate terminal result");
	}
}

namespace gglab
{
	using namespace runtime_test;

	void RunCaptureLifecycleIntegrationSelfTests(SelfTestContext& context) noexcept
	{
		RuntimeTestDirectory directory("capture");
		const auto& root = directory.GetPath();
		RunOwnedJobRoutingScenario(context, root / "owned-job-routing");
		RunOwnedJobFailureScenario(context, root / "owned-job-failed-fenced", FrameEndInjection::FatalFenced, false);
		RunOwnedJobFailureScenario(context, root / "owned-job-failed-unfenced", FrameEndInjection::FatalUnfenced, false);
		RunOwnedJobFailureScenario(context, root / "owned-job-unknown", FrameEndInjection::Completed, true);
		RunOwnedWriterDrainScenario(context, root / "owned-writer-drain");
		RunAdmittedJobScenario(context, root / "admitted-job", false);
		RunAdmittedJobScenario(context, root / "admitted-job-missing", true);
		RunAdmittedJobScenario(context, root / "admitted-job-multiple-images", false, true);
		RunJobCancellationScenario(context, root / "cancel-before-submit", true);
		RunJobCancellationScenario(context, root / "cancel-shared-submission", false);
		RunRequiredPlanOutcomeScenario(context, root / "required-plan-late", 0, false);
		RunRequiredPlanOutcomeScenario(context, root / "required-plan-reference", 3, false);
		RunRequiredPlanOutcomeScenario(context, root / "required-plan-missing", 0, true);
	}
}
