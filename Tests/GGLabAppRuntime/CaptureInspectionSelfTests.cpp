#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureMetadata.h"
#include "CaptureCoordinatorTestCases.h"
#include "CaptureTestServices.h"
#include "FrameCaptureCoordinatorSelfTests.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/GIFieldObservation.h"
#include "GGLabRuntime/Graphics/Pipeline/GIFieldReadiness.h"
#include "GGLabRuntime/Graphics/Pipeline/GIProbePicking.h"
#include "GGLabRuntime/Graphics/Pipeline/GIReceiverObservation.h"
#include "GGLabTestCore/SelfTest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <ranges>

namespace gglab::capture_test
{
	void RunGIProbePickingTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("probe-picking");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({ .m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true });
		CaptureTestJobs jobs(coordinator, context);
		auto summary = std::make_shared<GIFieldSummary>();
		summary->m_Field = { .m_Generation = 73, .m_SceneRevision = 9 };
		summary->m_Counts = { 4, 4, 4 };
		auto& display = summary->m_Visualization;
		display.m_SettingsRevision = 2;
		display.m_Settings.m_Enabled = true;
		display.m_Available = display.m_PickingAvailable = true;
		display.m_View = { .m_Revision = 3, .m_RenderExtent = { 64, 32 },
			.m_DisplayExtent = { 64, 32 }, .m_ProjectionSupported = true, .m_Valid = true };
		GIProbePickingRequest request{ .m_Token = 1, .m_SourceFrame = 16, .m_SettingsRevision = 2,
			.m_Field = summary->m_Field, .m_View = display.m_View, .m_Settings = display.m_Settings,
			.m_Mapping = { .m_GuiExtent = { 64, 32 } }, .m_Pixel = { 32, 16 } };
		const auto plan = [&request]()
			{
				CapturePlan result;
				result.m_RequiredContentId = "gglab.lab.test";
				result.m_GIProbePicking = request;
				result.m_RequiredProducts = { ResourceCaptureProduct{ {
					{ .m_Resource = "GI.Probes.PickID" }, { .m_Resource = "GI.Probes.PickDepth" } } } };
				return result;
			};
		for (uint32_t change = 0; change < 4; ++change)
		{
			auto invalid = plan();
			if (change == 0)
			{
				invalid.m_RequiredContentId.clear();
			}
			if (change == 1)
			{
				invalid.m_GIProbePicking->m_Token = 0;
			}
			if (change == 2)
			{
				std::get<ResourceCaptureProduct>(invalid.m_RequiredProducts[0]).m_Targets.pop_back();
			}
			if (change == 3)
			{
				invalid.m_GIProbePicking->m_Pixel[0] = 64;
			}
			context.Check(coordinator.CheckJobAdmission(invalid).m_Reason == CaptureAdmissionReason::InvalidPlan,
				"Malformed or incomplete picking plans reject without issuing Runtime work");
		}
		auto state = MakeFrameState(false);
		state.m_Readiness.m_GIField = GIFieldCaptureState{ .m_Generation = 73, .m_Summary = summary };
		const auto first = jobs.Admit(plan());
		++request.m_Token;
		const auto deferred = jobs.Admit(plan());
		coordinator.BeginFrame(state);
		context.Check(control.m_Issued.size() == 1 && coordinator.GetFrameGIProbePicking()->m_Token == 1 &&
			coordinator.GetJobSnapshot(deferred)->m_Stage == CaptureJobStage::Waiting,
			"Only one distinct pick issues per frame; conflicting clicks wait in bounded existing jobs");
		coordinator.OnFrameSubmitted();
		control.Complete(control.m_Issued[0].m_Id);
		coordinator.Update();
		const auto completed = jobs.TakeResults();
		context.Check(completed.size() == 1 && completed[0].m_JobId == first &&
			completed[0].m_Products[0].m_Metadata->m_GIProbePicking->m_Validity == GIProbePickingValidity::MalformedPayload,
			"Malformed completed transport remains explicit evidence instead of becoming NoHit");
		++display.m_SettingsRevision;
		++state.m_FrameSerial;
		coordinator.BeginFrame(state);
		const auto stale = jobs.TakeResults();
		context.Check(stale.size() == 1 && stale[0].m_JobId == deferred &&
			stale[0].m_FailureCode == "GIProbePickingSettingsMismatch" && control.m_Issued.size() == 1 &&
			!coordinator.GetFrameGIProbePicking(), "Deferred picks revalidate bindings before any GPU request");
		--display.m_SettingsRevision;
		for (uint32_t change = 0; change < 4; ++change)
		{
			auto current = state;
			if (change == 0)
			{
				current.m_LabId = "gglab.lab.other";
			}
			if (change == 1)
			{
				current.m_DisplayDpiScale = 1.25f;
			}
			if (change == 2)
			{
				++summary->m_Field.m_SceneRevision;
			}
			if (change == 3)
			{
				current.m_FrameSerial = request.m_SourceFrame;
			}
			const auto id = jobs.Admit(plan());
			coordinator.BeginFrame(current);
			const auto failed = jobs.TakeResults();
			context.Check(failed.size() == 1 && failed[0].m_JobId == id &&
				failed[0].m_Status == CaptureJobStatus::Failed && !failed[0].m_FailureCode.empty() &&
				control.m_Issued.size() == 1, "Content, DPI, scene and future-frame mismatches issue no GPU work");
			if (change == 2)
			{
				--summary->m_Field.m_SceneRevision;
			}
		}
		coordinator.FinalizeAfterRenderHost();
	}

	void RunGIProbeInspectionTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("probe-inspection");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({ .m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true });
		CaptureTestJobs jobs(coordinator, context);
		const auto plan = [](uint32_t x, uint64_t generation = 73)
			{
				CapturePlan result;
				result.m_RequiredProducts = { ResourceCaptureProduct{
					{ { .m_Resource = "GI.Field.ProbeInspection" } } } };
				result.m_GIProbeInspection = GIProbeInspectionRequest{
					.m_Coordinate = { x, 0, 0 }, .m_ExpectedGeneration = generation,
					.m_ExpectedRefreshEpoch = 2 };
				return result;
			};
		auto summary = std::make_shared<GIFieldSummary>();
		summary->m_Field = { .m_Generation = 73, .m_RefreshEpoch = 2, .m_EpochOriginUpdate = 80 };
		summary->m_Counts = { 4, 4, 4 };
		summary->m_Spacing = 2;
		summary->m_SubmittedUpdates = 82;
		summary->m_InspectionAvailable = true;
		{
			auto held = *summary;
			context.Check(GetGIFieldFrameUpdate(held) == 83, "An updating frame observes the update it records");
			held.m_UpdateLimit = 82;
			held.m_UpdateHeld = true;
			const auto json = nlohmann::json::parse(SerializeGIFieldSummary(held));
			context.Check(GetGIFieldFrameUpdate(held) == 82 && json["frameUpdate"] == 82 &&
				json["updateHeld"] == true && json["updateLimit"] == 82,
				"A held frame observes and publishes the last recorded update, not a new one");
			held.m_UpdateHeld = false;
			held.m_Invalid = true;
			const auto invalid = nlohmann::json::parse(SerializeGIFieldSummary(held));
			context.Check(GetGIFieldFrameUpdate(held) == 82 && invalid["frameUpdate"] == 82,
				"An invalid field cannot attribute consumers to an update it will not record");
			held.m_RefreshWaits = 5;
			held.m_RefreshWaitMilliseconds = 1.5;
			const auto waits = nlohmann::json::parse(SerializeGIFieldSummary(held))["refreshWaits"];
			context.Check(waits["count"] == 5 && waits["milliseconds"] == 1.5,
				"The summary publishes the cost of settling open refreshes");
		}
		auto state = MakeFrameState(false);
		state.m_Readiness.m_GIField = GIFieldCaptureState{ .m_Generation = 73, .m_Summary = summary };
		const uint64_t first = jobs.Admit(plan(1));
		const uint64_t second = jobs.Admit(plan(2));
		const uint64_t stale = jobs.Admit(plan(1, 72));
		coordinator.BeginFrame(state);
		context.Check(control.m_Issued.size() == 1 && coordinator.GetFrameGIProbeInspection() &&
			coordinator.GetFrameGIProbeInspection()->m_Coordinate[0] == 1 &&
			coordinator.GetJobSnapshot(second)->m_Stage == CaptureJobStage::Waiting,
			"Next-frame inspection diagnoses non-Ready fields and defers conflicting coordinates before issue");
		coordinator.OnFrameSubmitted();
		const auto results = jobs.TakeResults();
		context.Check(results.size() == 1 && results[0].m_JobId == stale &&
			results[0].m_FailureCode == "GIProbeIdentityMismatch",
			"A replacement field cannot silently satisfy an earlier generation's coordinate");
		context.Check(coordinator.CancelJob(first) == CaptureCancelResult::Cancelled,
			"Inspection cancellation uses the existing job delivery contract");
		++state.m_FrameSerial;
		coordinator.BeginFrame(state);
		context.Check(coordinator.GetFrameGIProbeInspection() &&
			coordinator.GetFrameGIProbeInspection()->m_Coordinate[0] == 2,
			"A queued coordinate gets its own later producing frame");
		coordinator.OnFrameSubmitted();
		GGLAB_UNUSED(coordinator.CancelJob(second));
		auto withoutResource = plan(0);
		withoutResource.m_RequiredProducts = { ImageCaptureProduct{} };
		context.Check(coordinator.CheckJobAdmission(withoutResource).m_Reason == CaptureAdmissionReason::InvalidPlan,
			"A typed inspection cannot omit its required compact resource");
		auto math = plan(0);
		math.m_GIProbeInspection->m_IntegrationReplayUpdate = 563;
		GIProbeInspectionResult witness;
		const auto ordinaryJson = nlohmann::json::parse(SerializeGIProbeInspection(witness));
		witness.m_Request = *math.m_GIProbeInspection;
		const auto witnessJson = nlohmann::json::parse(SerializeGIProbeInspection(witness));
		context.Check(ordinaryJson["schemaVersion"] == 1 &&
			!ordinaryJson["request"].contains("integrationReplayUpdate") &&
			witnessJson["schemaVersion"] == 2 && witnessJson["request"]["integrationReplayUpdate"] == 563,
			"Only opt-in integration inspection publishes the extended v2 request schema");
		context.Check(coordinator.CheckJobAdmission(math).m_Reason == CaptureAdmissionReason::InvalidPlan,
			"An integration witness cannot omit its same-frame raw resource");
		std::get<ResourceCaptureProduct>(math.m_RequiredProducts[0]).m_Targets.push_back(
			{ .m_Resource = "GI.Field.IntegrationInspection" });
		context.Check(coordinator.CheckJobAdmission(math).m_Status == CaptureAdmissionStatus::Accepted,
			"A generation-bound integration witness uses ordinary inspection admission");
		std::get<ResourceCaptureProduct>(math.m_RequiredProducts[0]).m_Targets.back().m_Mip = 1;
		context.Check(coordinator.CheckJobAdmission(math).m_Reason == CaptureAdmissionReason::InvalidPlan,
			"Integration witness subresource mismatches reject before GPU work");
		const auto summaryJson = nlohmann::json::parse(SerializeGIFieldSummary(*summary));
		context.Check(summaryJson["completedMeasurement"].is_null() &&
			summaryJson["field"]["epochOriginUpdate"] == 80 &&
			summaryJson["classificationCounts"].is_null(),
			"A fresh epoch has no relabelled measurement or invented classification counts");
	}

	void RunGIReceiverInspectionTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("receiver-inspection");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({ .m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true });
		CaptureTestJobs jobs(coordinator, context);
		const auto plan = [](uint32_t x, uint64_t viewRevision = 4)
			{
				CapturePlan result;
				result.m_RequiredProducts = { ResourceCaptureProduct{ { { .m_Resource = "GI.Field.ReceiverInspection" } } } };
				result.m_GIReceiverInspection = GIReceiverInspectionRequest{ .m_Pixel = { x, 10 },
					.m_ExpectedGeneration = 73, .m_ExpectedViewRevision = viewRevision };
				return result;
			};
		auto summary = std::make_shared<GIFieldSummary>();
		summary->m_Field = { .m_Generation = 73, .m_RefreshEpoch = 2, .m_EpochOriginUpdate = 80 };
		summary->m_Counts = { 4, 4, 4 };
		summary->m_SubmittedUpdates = 82;
		summary->m_InspectionAvailable = true;
		summary->m_ReceiverView = { .m_Revision = 4, .m_RenderExtent = { 64, 32 }, .m_DisplayExtent = { 64, 32 },
			.m_ReceiversAvailable = true, .m_Supported = true };
		summary->m_ReceiverInspectionAvailable = true;
		summary->m_ReceiverMarkersAvailable = true;
		auto state = MakeFrameState(false);
		state.m_Readiness.m_GIField = GIFieldCaptureState{ .m_Generation = 73, .m_Summary = summary };
		const auto first = jobs.Admit(plan(10));
		const auto second = jobs.Admit(plan(11));
		const auto stale = jobs.Admit(plan(10, 3));
		coordinator.BeginFrame(state);
		context.Check(control.m_Issued.size() == 1 && coordinator.GetFrameGIReceiverInspection() &&
			coordinator.GetFrameGIReceiverInspection()->m_Pixel[0] == 10 &&
			coordinator.GetJobSnapshot(second)->m_Stage == CaptureJobStage::Waiting,
			"Receiver queries share ordinary jobs and defer conflicting pixels before freezing a frame");
		coordinator.OnFrameSubmitted();
		const auto failures = jobs.TakeResults();
		context.Check(failures.size() == 1 && failures[0].m_JobId == stale &&
			failures[0].m_FailureCode == "GIReceiverIdentityMismatch", "Stale view bindings fail explicitly");
		control.Complete(control.m_Issued.front().m_Id);
		coordinator.Update();
		const auto completed = jobs.TakeResults();
		context.Check(completed.size() == 1 && completed[0].m_JobId == first && completed[0].m_Products[0].m_Metadata &&
			completed[0].m_Products[0].m_Metadata->m_GIReceiverObservation->m_FieldUpdate == 83 &&
			completed[0].m_Products[0].m_Metadata->m_GIReceiverObservation->m_ReadinessUpdate == 3 &&
			completed[0].m_Products[0].m_Metadata->m_GIReceiverInspection->m_Validity == GIReceiverInspectionValidity::MalformedPayload,
			"Fence-completed transport freezes field/view stamps and preserves malformed evidence as invalid");
		GGLAB_UNUSED(coordinator.CancelJob(second));
		GGLAB_UNUSED(jobs.TakeResults());
		auto unsupported = std::make_shared<GIFieldSummary>(*summary);
		unsupported->m_ReceiverView.m_Supported = false;
		state.m_Readiness.m_GIField->m_Summary = unsupported;
		const auto unsupportedId = jobs.Admit(plan(10));
		++state.m_FrameSerial;
		coordinator.BeginFrame(state);
		const auto unavailable = jobs.TakeResults();
		context.Check(unavailable.size() == 1 && unavailable[0].m_JobId == unsupportedId &&
			unavailable[0].m_FailureCode == "GIReceiverViewUnsupported" && !coordinator.GetFrameGIReceiverInspection(),
			"Unsupported receiver views reject without changing TAA, resolution or scheduling GPU work");
		auto numericSummary = std::make_shared<GIFieldSummary>(*summary);
		numericSummary->m_ReceiverMarkersAvailable = false;
		state.m_Readiness.m_GIField->m_Summary = numericSummary;
		const auto markerUnavailable = jobs.Admit(plan(10));
		auto numericPlan = plan(10);
		numericPlan.m_GIReceiverInspection->m_Markers = false;
		const auto numeric = jobs.Admit(numericPlan);
		const auto ordinary = jobs.Admit({ .m_RequiredProducts = { ResourceCaptureProduct{
			{ { .m_Resource = "GI.Receiver.Position" } } } } });
		const auto beforeIssue = control.m_Issued.size();
		++state.m_FrameSerial;
		coordinator.BeginFrame(state);
		coordinator.OnFrameSubmitted();
		const auto markerFailures = jobs.TakeResults();
		context.Check(markerFailures.size() == 1 && markerFailures[0].m_JobId == markerUnavailable &&
			markerFailures[0].m_Status == CaptureJobStatus::Failed &&
			markerFailures[0].m_Publication == CaptureJobPublication::NotPublished &&
			markerFailures[0].m_FailureCode == "GIReceiverMarkersUnavailable" &&
			control.m_Issued.size() == beforeIssue + 2 && coordinator.GetFrameGIReceiverInspection() &&
			!coordinator.GetFrameGIReceiverInspection()->m_Markers,
			"Missing marker pipelines reject paired-marker requests before issue while numeric and ordinary captures proceed");
		for (size_t index = beforeIssue; index < control.m_Issued.size(); ++index)
		{
			control.Complete(control.m_Issued[index].m_Id);
		}
		coordinator.Update();
		const auto numericCompleted = jobs.TakeResults();
		context.Check(numericCompleted.size() == 2 &&
			std::ranges::all_of(numericCompleted, [=](const auto& result)
			{
				return (result.m_JobId == numeric || result.m_JobId == ordinary) &&
					result.m_Status == CaptureJobStatus::Completed &&
					result.m_Publication == CaptureJobPublication::Published;
			}), "Numeric inspection and independent captures publish without the optional marker pipeline");
		auto querySummary = std::make_shared<GIFieldSummary>(*numericSummary);
		querySummary->m_ReceiverInspectionAvailable = false;
		state.m_Readiness.m_GIField->m_Summary = querySummary;
		const auto queryUnavailable = jobs.Admit(numericPlan);
		++state.m_FrameSerial;
		coordinator.BeginFrame(state);
		const auto queryFailures = jobs.TakeResults();
		context.Check(queryFailures.size() == 1 && queryFailures[0].m_JobId == queryUnavailable &&
			queryFailures[0].m_FailureCode == "GIReceiverUnavailable" &&
			control.m_Issued.size() == beforeIssue + 2 && !coordinator.GetFrameGIReceiverInspection(),
			"Missing receiver query pipelines fail numeric inspection before scheduling GPU work");
		const auto supportJson = nlohmann::json::parse(SerializeGIFieldSummary(*querySummary));
		context.Check(supportJson["inspectionAvailable"] == true &&
			supportJson["receiverInspectionAvailable"] == false && supportJson["receiverMarkersAvailable"] == false,
			"Discovery distinguishes field data, receiver query and receiver marker availability");
		GIReceiverInspectionResult zero;
		zero.m_Validity = GIReceiverInspectionValidity::NoValidSupport;
		zero.m_CageEvaluated = true;
		zero.m_Corners[0].m_Reason = GIReceiverCornerReason::NotActive;
		const auto json = nlohmann::json::parse(SerializeGIReceiverInspection(zero));
		context.Check(json["corners"].size() == 8 && json["corners"][0]["normalizedWeight"].is_null() &&
			json["corners"][0]["visibility"].is_null() && !json["normalizationReason"].is_null(),
			"Zero total weight and excluded sampling remain unavailable rather than fabricated zeros");
	}

	void RunGIInspectionCancellationTests(SelfTestContext& context) noexcept
	{
		for (bool receiver : { false, true })
		{
			for (bool submitted : { false, true })
			{
				TemporaryDirectory directory("inspection-close");
				FakeCaptureControl control;
				FrameCaptureCoordinator coordinator({ .m_Capture = &control,
					.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true });
				CaptureTestJobs guiJobs(coordinator, context);
				CaptureTestJobs sessionJobs(coordinator, context);
				auto summary = std::make_shared<GIFieldSummary>();
				summary->m_Field = { .m_Generation = 73, .m_RefreshEpoch = 2 };
				summary->m_Counts = { 4, 4, 4 };
				summary->m_InspectionAvailable = true;
				summary->m_ReceiverInspectionAvailable = true;
				summary->m_ReceiverMarkersAvailable = true;
				summary->m_ReceiverView = { .m_Revision = 4, .m_RenderExtent = { 32, 32 },
					.m_DisplayExtent = { 32, 32 }, .m_ReceiversAvailable = true, .m_Supported = true };
				auto state = MakeFrameState(false);
				state.m_Readiness.m_GIField = GIFieldCaptureState{ .m_Generation = 73, .m_Summary = summary };
				CapturePlan plan;
				if (receiver)
				{
					plan.m_RequiredProducts = { ResourceCaptureProduct{
						{ { .m_Resource = "GI.Field.ReceiverInspection" } } } };
					plan.m_GIReceiverInspection = GIReceiverInspectionRequest{ .m_Pixel = { 10, 10 },
						.m_ExpectedGeneration = 73, .m_ExpectedViewRevision = 4 };
				}
				else
				{
					plan.m_RequiredProducts = { ResourceCaptureProduct{
						{ { .m_Resource = "GI.Field.ProbeInspection" } } } };
					plan.m_GIProbeInspection = GIProbeInspectionRequest{ .m_Coordinate = { 1, 0, 0 },
						.m_ExpectedGeneration = 73 };
				}
				const auto gui = guiJobs.Admit(plan);
				const auto session = sessionJobs.Admit(plan);
				if (submitted)
				{
					coordinator.BeginFrame(state);
					coordinator.OnFrameSubmitted();
				}
				context.Check(coordinator.CancelJob(gui) == CaptureCancelResult::Cancelled &&
					coordinator.GetIntakeSnapshot().m_DrainingRuntimeRequests == (submitted ? 1u : 0u),
					"Closing an inspection cancels its own receipt and retains only submitted work for drain");
				const auto cancelled = guiJobs.TakeResults();
				context.Check(cancelled.size() == 1 && cancelled[0].m_JobId == gui &&
					cancelled[0].m_Status == CaptureJobStatus::Cancelled &&
					cancelled[0].m_Publication == CaptureJobPublication::NotPublished && sessionJobs.TakeResults().empty(),
					"GUI cancellation can be consumed independently while the session inspection remains active");
				if (!submitted)
				{
					coordinator.BeginFrame(state);
					coordinator.OnFrameSubmitted();
				}
				context.Check(control.m_Issued.size() == (submitted ? 2u : 1u),
					"An inspection closed before issue schedules no GPU work and preserves the session request");
				for (const auto& issued : control.m_Issued)
				{
					control.Complete(issued.m_Id);
				}
				coordinator.Update();
				const auto completed = sessionJobs.TakeResults();
				context.Check(completed.size() == 1 && completed[0].m_JobId == session &&
					completed[0].m_Status == CaptureJobStatus::Completed &&
					completed[0].m_Publication == CaptureJobPublication::Published && guiJobs.TakeResults().empty() &&
					coordinator.GetIntakeSnapshot().m_DrainingRuntimeRequests == 0 &&
					control.GetUnfinishedRequestCount() == 0,
					"Late GPU completion drains without reviving the closed inspection or consuming the session result");
			}
		}
	}
}

namespace gglab
{
	using namespace capture_test;

	void RunCaptureInspectionSelfTests(SelfTestContext& context) noexcept
	{
		RunGIProbeInspectionTests(context);
		RunGIProbePickingTests(context);
		RunGIReceiverInspectionTests(context);
		RunGIInspectionCancellationTests(context);
	}

	void RunGIProbeInspectionSelfTests(SelfTestContext& context) noexcept
	{
		RunGIProbeInspectionTests(context);
		RunGIInspectionCancellationTests(context);
	}
}
