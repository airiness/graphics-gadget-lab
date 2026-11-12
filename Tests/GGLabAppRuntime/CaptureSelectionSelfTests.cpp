#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/FrameCaptureReadiness.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "Capture/NonFiniteWatchCoordinator.h"
#include "Capture/RequiredFrameState.h"
#include "CaptureCoordinatorTestCases.h"
#include "CaptureTestServices.h"
#include "FrameCaptureCoordinatorSelfTests.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalHistoryTypes.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabTestCore/SelfTest.h"
#include "Lab/LabParameter.h"
#include "Parameters/HostViewParameters.h"
#include "SequenceTestServices.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gglab::capture_test
{
	void RunNextFrameTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-next");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({
			.m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(),
			.m_ImageEncoder = &EncodeTestPng,
			.m_WriteOnCallingThread = true,
		});
		CaptureTestJobs coordinatorJobs(coordinator, context);

		const uint64_t id = coordinatorJobs.Admit({
			.m_RequiredProducts = { ImageCaptureProduct{ CaptureImageSource::Composited } },
			.m_Label = "loading \"probe\"",
		});
		context.Check(id != 0 && control.m_Issued.empty() &&
			coordinator.GetUnfinishedRequestCount() == 1,
			"Requests wait for the next frame before reaching the Runtime");

		coordinator.BeginFrame(MakeFrameState(false));
		FrameCaptureSceneInputs sceneInputs;
		sceneInputs.m_Exposure.m_ManualEV100 = 15.0f;
		coordinator.SetFrameSceneInputs(17, sceneInputs);
		sceneInputs.m_Exposure.m_ManualEV100 = 3.0f;
		coordinator.SetFrameSceneInputs(18, sceneInputs);
		context.Check(control.m_Issued.size() == 1 &&
			control.m_Issued[0].m_Source == FrameCaptureSource::Composited,
			"A next-frame capture is issued even while content is still loading");
		coordinator.OnFrameSubmitted();
		if (control.m_Issued.empty())
		{
			return;
		}

		control.Complete(control.m_Issued[0].m_Id);
		coordinator.Update();
		const std::vector<CaptureJobResult> results = coordinatorJobs.TakeResults();
		const bool completed = results.size() == 1 && results[0].m_JobId == id &&
			results[0].m_Status == CaptureJobStatus::Completed &&
			results[0].m_Products.front().m_Metadata;
		context.Check(completed && coordinator.GetUnfinishedRequestCount() == 0,
			"A completed Runtime capture finishes once its files are written");
		if (!completed)
		{
			return;
		}

		const CaptureJobResult& result = results[0];
		const FrameCaptureMetadata& metadata = *result.m_Products.front().m_Metadata;
		context.Check(metadata.m_SceneInputs && metadata.m_SceneInputs->m_Exposure.m_ManualEV100 == 15.0f,
			"Only the issued frame's built scene inputs reach its capture metadata");
		context.Check(std::filesystem::exists(ProductFile(result, ".png")) &&
			std::filesystem::exists(ProductFile(result, ".json")) &&
			HasPngSignature(ProductFile(result, ".png")) &&
			ProductFile(result, ".png").parent_path() == directory.GetPath() &&
			!HasPartialFile(directory.GetPath()),
			"The PNG and its metadata sidecar are published without partial files");
		context.Check(metadata.m_FrameSerial == 17 && metadata.m_Width == 2 &&
			metadata.m_Height == 2 && metadata.m_DisplayFormat == "B8G8R8A8Unorm" &&
			metadata.m_LabId == "gglab.lab.test" && metadata.m_FrameIndex == 42 &&
			metadata.m_Timing == FrameCaptureTiming::NextFrame &&
			!metadata.m_Readiness.IsReady() &&
			metadata.m_ImageFile == ProductFile(result, ".png").filename().string(),
			"Metadata records the frame, content, image and readiness at issue time");

		const std::string json = ReadText(ProductFile(result, ".json"));
		context.Check(json.find("\"schemaVersion\": 1") != std::string::npos &&
			json.find(std::format("\"requestId\": {}", id)) != std::string::npos &&
			json.find("\"label\": \"loading \\\"probe\\\"\"") != std::string::npos &&
			json.find("\"source\": \"composited\"") != std::string::npos &&
			json.find("\"ready\": false") != std::string::npos,
			"The sidecar is schema-versioned JSON with escaped strings");
		context.Check(ProductFile(result, ".png").filename().string().starts_with("loading__probe_.") &&
			!result.m_ManifestPath.empty() && !result.m_BundleId.empty(),
			"File names retain the sanitized label and a qualified bundle identity");
	}

	constexpr std::string_view RequiredStateRun = "0123456789abcdef0123456789abcdef";

	[[nodiscard]] FrameCaptureFrameState MakeControlledFrameState(
		uint64_t serial, uint64_t hostViewRevision, bool temporalAA)
	{
		FrameCaptureFrameState state = MakeFrameState(true);
		state.m_FrameSerial = serial;
		state.m_HostViewControl = { std::string(RequiredStateRun), 7, hostViewRevision };
		state.m_HostViewProjection = { HostViewProjectionEntry{
			.m_Id = LabParameterId("view.taa.enabled"),
			.m_Type = LabParameterType::Bool,
			.m_Value = temporalAA,
			.m_Source = HostViewValueSource::Host,
			.m_InheritedSource = HostViewValueSource::Content,
			.m_Override = temporalAA,
		} };
		state.m_Temporal.m_Consumers = {
			{ "temporal-aa", temporalAA, temporalAA ? "active" : "disabled", temporalAA ? "none" : "not-requested", {} },
			{ "reference", false, "disabled", "not-requested", {} },
		};
		return state;
	}

	[[nodiscard]] std::shared_ptr<RequiredFrameState> MakeRequiredFrameState(
		bool temporalAA, uint64_t minHostViewRevision)
	{
		auto required = std::make_shared<RequiredFrameState>();
		required->m_RunIdentity = RequiredStateRun;
		required->m_ContentIdentity = 7;
		required->m_MinHostViewRevision = minHostViewRevision;
		required->m_Values = { { LabParameterId("view.taa.enabled"), LabParameterType::Bool, temporalAA,
			HostViewValueSource::Host } };
		required->m_TemporalConsumers = { { TemporalConsumer::TemporalAA,
			temporalAA ? TemporalConsumerStatus::Active : TemporalConsumerStatus::Disabled, std::nullopt } };
		required->m_ReferenceSampling = false;
		return required;
	}

	void RunFrameSelectionDeadlineTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("capture-selection-deadline");
		const auto plan = []()
			{
				return CapturePlan{ .m_RequiredProducts = { ImageCaptureProduct{} },
					.m_WaitForCapacity = true, .m_RequiredFrameState = MakeRequiredFrameState(false, 0),
					.m_FrameSelectionTimeout = std::chrono::seconds(1) };
			};
		for (int blocker = 0; blocker < 4; ++blocker)
		{
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
				.m_WriteOnCallingThread = true, .m_JobLimits = { .m_MaxActiveJobs = 1, .m_MaxReceipts = 2 },
				.m_RunIdentity = std::string(RequiredStateRun) });
			auto waitingPlan = plan();
			waitingPlan.m_Timing = FrameCaptureTiming::AfterReady;
			waitingPlan.m_SettleFrames = blocker == 2 ? 100 : 0;
			const auto admitted = coordinator.TrySubmit(std::move(waitingPlan));
			const auto deadline = coordinator.GetNextFrameSelectionDeadline().value();
			if (blocker != 0)
			{
				auto state = MakeControlledFrameState(100, 0, false);
				state.m_Readiness = MakeFrameState(blocker != 1).m_Readiness;
				control.m_RequestAvailability = blocker == 3 ?
					FrameCaptureAvailability::Busy : FrameCaptureAvailability::Available;
				coordinator.BeginFrame(std::move(state));
				coordinator.OnFrameSubmitted();
			}
			coordinator.ExpireFrameSelections(deadline - std::chrono::nanoseconds(1));
			context.Check(coordinator.GetIntakeSnapshot().m_ActiveJobs == 1 && control.m_Issued.empty(),
				"No frame, readiness, settling and capacity waits retain the job until its absolute deadline");
			coordinator.ExpireFrameSelections(deadline);
			const auto intake = coordinator.GetIntakeSnapshot();
			const auto result = coordinator.TakeJobResult(admitted.m_JobId);
			control.m_RequestAvailability = FrameCaptureAvailability::Available;
			coordinator.BeginFrame(MakeControlledFrameState(101, 0, false));
			coordinator.OnFrameSubmitted();
			context.Check(result && result->m_Status == CaptureJobStatus::Failed &&
				result->m_FailureCode == "FrameSelectionExpired" &&
				result->m_Publication == CaptureJobPublication::NotPublished &&
				intake.m_ActiveJobs == 0 && intake.m_ReservedReceipts == 1 && intake.m_TerminalReceipts == 1 &&
				control.m_Issued.empty() && !coordinator.GetNextFrameSelectionDeadline() &&
				!coordinator.TakeJobResult(admitted.m_JobId),
				"Expiry releases active capacity, retains one consumable failure and cannot issue after resume");
		}
		for (const auto& [submitted, grouped] : { std::pair{ false, false }, std::pair{ true, false },
			std::pair{ false, true }, std::pair{ true, true } })
		{
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
				.m_WriteOnCallingThread = true, .m_RunIdentity = std::string(RequiredStateRun) });
			auto issuedPlan = plan();
			if (grouped)
			{
				issuedPlan.m_RequiredProducts.push_back(GraphCaptureProduct{});
			}
			const auto id = coordinator.TrySubmit(std::move(issuedPlan)).m_JobId;
			const auto deadline = coordinator.GetNextFrameSelectionDeadline().value();
			coordinator.BeginFrame(MakeControlledFrameState(110, 0, false));
			coordinator.ExpireFrameSelections(deadline);
			context.Check(coordinator.GetIntakeSnapshot().m_ActiveJobs == 1 &&
				!coordinator.GetNextFrameSelectionDeadline(),
				"An issued open frame completes its submit-or-abort decision before selection expiry");
			if (submitted)
			{
				coordinator.OnFrameSubmitted();
			}
			else
			{
				coordinator.OnFrameAborted();
			}
			coordinator.ExpireFrameSelections(deadline);
			const auto expired = submitted ? std::optional<CaptureJobResult>{} : coordinator.TakeJobResult(id);
			context.Check(submitted ? coordinator.GetIntakeSnapshot().m_ActiveJobs == 1 :
				(expired && expired->m_FailureCode == "FrameSelectionExpired" &&
					coordinator.GetIntakeSnapshot().m_DrainingRuntimeRequests == (grouped ? 2u : 1u) &&
					control.GetUnfinishedRequestCount() == (grouped ? 2u : 1u)),
				"Submission ends the selection deadline; abort expiry retains Runtime work after receipt consumption");
			for (const auto& issued : control.m_Issued)
			{
				control.Complete(issued.m_Id);
			}
			coordinator.Update();
			const auto completed = coordinator.TakeJobResult(id);
			context.Check(submitted ?
				(completed && completed->m_Publication == CaptureJobPublication::Published) :
				(!completed && coordinator.GetIntakeSnapshot().m_DrainingRuntimeRequests == 0),
				"Late readback publishes only submitted matching jobs and drains expired jobs without another receipt");
		}
		{
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_RunIdentity = std::string(RequiredStateRun) });
			const auto id = coordinator.TrySubmit(plan()).m_JobId;
			const auto deadline = coordinator.GetNextFrameSelectionDeadline().value();
			coordinator.BeginFrame(MakeControlledFrameState(120, 0, false));
			control.Busy(control.m_Issued.front().m_Id);
			coordinator.Update();
			coordinator.OnFrameAborted();
			context.Check(coordinator.GetNextFrameSelectionDeadline() == deadline &&
				coordinator.GetJobSnapshot(id)->m_Stage == CaptureJobStage::Waiting,
				"A Busy unsubmitted attempt returns to Waiting with the original admission deadline");
			coordinator.ExpireFrameSelections(deadline);
			context.Check(coordinator.TakeJobResult(id)->m_FailureCode == "FrameSelectionExpired",
				"A retry that reaches its original deadline fails instead of acquiring a new wait budget");
			auto ordinary = plan();
			ordinary.m_RequiredFrameState.reset();
			context.Check(coordinator.CheckJobAdmission(ordinary).m_Status == CaptureAdmissionStatus::Rejected,
				"A typed selection timeout without a predicate is invalid");
			ordinary.m_FrameSelectionTimeout.reset();
			const auto legacy = coordinator.TrySubmit(std::move(ordinary));
			coordinator.ExpireFrameSelections(deadline + std::chrono::hours(1));
			context.Check(legacy.m_JobId != 0 && !coordinator.GetNextFrameSelectionDeadline() &&
				coordinator.GetIntakeSnapshot().m_ActiveJobs == 1,
				"Plans without a predicate retain their existing unlimited frame-selection behavior");
			GGLAB_UNUSED(coordinator.CancelJob(legacy.m_JobId));
			GGLAB_UNUSED(coordinator.TakeJobResult(legacy.m_JobId));
			auto defaultPlan = plan();
			defaultPlan.m_FrameSelectionTimeout.reset();
			const auto beforeAdmission = std::chrono::steady_clock::now();
			const auto defaultJob = coordinator.TrySubmit(std::move(defaultPlan));
			context.Check(coordinator.GetNextFrameSelectionDeadline().value() >=
				beforeAdmission + MaxFrameSelectionTimeout,
				"Existing predicate callers acquire the finite default selection deadline");
			GGLAB_UNUSED(coordinator.CancelJob(defaultJob.m_JobId));
			GGLAB_UNUSED(coordinator.TakeJobResult(defaultJob.m_JobId));
			for (int timeout : { -1, 0, 300001 })
			{
				auto invalid = plan();
				invalid.m_FrameSelectionTimeout = std::chrono::milliseconds(timeout);
				context.Check(coordinator.TrySubmit(std::move(invalid)).m_Status == CaptureAdmissionStatus::Rejected &&
					coordinator.GetIntakeSnapshot().m_ReservedReceipts == 0,
					"Out-of-range typed deadlines reject without reserving a receipt");
			}
		}
	}

	void RunRequiredFrameStateTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-required-state");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({
			.m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(),
			.m_ImageEncoder = &EncodeTestPng,
			.m_WriteOnCallingThread = true,
			.m_RunIdentity = std::string(RequiredStateRun),
		});
		CaptureTestJobs coordinatorJobs(coordinator, context);
		const auto plan = [](std::shared_ptr<const RequiredFrameState> required)
			{
				return CapturePlan{ .m_RequiredProducts = { ImageCaptureProduct{} },
					.m_RequiredFrameState = std::move(required) };
			};

		auto wrongType = MakeRequiredFrameState(false, 0);
		wrongType->m_Values[0].m_Type = LabParameterType::Float;
		auto duplicate = MakeRequiredFrameState(false, 0);
		duplicate->m_Values.push_back(duplicate->m_Values[0]);
		auto contradictory = MakeRequiredFrameState(false, 0);
		contradictory->m_TemporalConsumers[0].m_DisableReason = TemporalConsumerDisableReason::None;
		auto disabledValueActive = MakeRequiredFrameState(false, 0);
		disabledValueActive->m_TemporalConsumers[0].m_Status = TemporalConsumerStatus::Active;
		auto enabledValueDisabled = MakeRequiredFrameState(true, 0);
		enabledValueDisabled->m_TemporalConsumers[0].m_Status = TemporalConsumerStatus::Disabled;
		auto unavailableUnrequested = MakeRequiredFrameState(true, 0);
		unavailableUnrequested->m_TemporalConsumers[0] = { TemporalConsumer::TemporalAA,
			TemporalConsumerStatus::Unavailable, TemporalConsumerDisableReason::NotRequested };
		auto referenceWithTemporalAA = MakeRequiredFrameState(true, 0);
		referenceWithTemporalAA->m_TemporalConsumers.clear();
		referenceWithTemporalAA->m_ReferenceSampling = true;
		auto gtaoOffActive = MakeRequiredFrameState(false, 0);
		gtaoOffActive->m_Values.push_back({ LabParameterId("view.gtao.enabled"), LabParameterType::Bool, false });
		gtaoOffActive->m_TemporalConsumers.push_back({ TemporalConsumer::AmbientOcclusion,
			TemporalConsumerStatus::Active, std::nullopt });
		auto otherRun = MakeRequiredFrameState(false, 0);
		otherRun->m_RunIdentity = "ffffffffffffffffffffffffffffffff";
		CapturePlan sequence = plan(MakeRequiredFrameState(false, 0));
		sequence.m_Sequence = FrameCaptureSequenceInfo{ .m_SequenceId = 1 };
		const auto reason = [&](const CapturePlan& candidate) { return coordinator.CheckJobAdmission(candidate).m_Reason; };
		context.Check(reason(plan(wrongType)) == CaptureAdmissionReason::RequiredFrameStateInvalid &&
			reason(plan(duplicate)) == CaptureAdmissionReason::RequiredFrameStateInvalid &&
			reason(plan(contradictory)) == CaptureAdmissionReason::RequiredFrameStateInvalid &&
			reason(sequence) == CaptureAdmissionReason::RequiredFrameStateInvalid &&
			reason(plan(otherRun)) == CaptureAdmissionReason::RequiredFrameStateUnavailable &&
			coordinator.GetIntakeSnapshot().m_ActiveJobs == 0,
			"Invalid predicates and another run's predicate reject before allocating a job");
		context.Check(reason(plan(disabledValueActive)) == CaptureAdmissionReason::RequiredFrameStateInvalid &&
			reason(plan(enabledValueDisabled)) == CaptureAdmissionReason::RequiredFrameStateInvalid &&
			reason(plan(unavailableUnrequested)) == CaptureAdmissionReason::RequiredFrameStateInvalid &&
			reason(plan(referenceWithTemporalAA)) == CaptureAdmissionReason::RequiredFrameStateInvalid &&
			reason(plan(gtaoOffActive)) == CaptureAdmissionReason::RequiredFrameStateInvalid,
			"Values and consumer states that the frame plan cannot resolve together reject at admission");

		const uint64_t waiting = coordinatorJobs.Admit(plan(MakeRequiredFrameState(false, 3)));
		coordinator.BeginFrame(MakeControlledFrameState(17, 2, false));
		coordinator.OnFrameSubmitted();
		context.Check(control.m_Issued.empty() && coordinatorJobs.TakeResults().empty(),
			"A frame below the required Host view revision keeps the plan waiting");
		coordinator.BeginFrame(MakeControlledFrameState(18, 3, false));
		coordinator.OnFrameSubmitted();
		context.Check(control.m_Issued.size() == 1 && control.m_Issued[0].m_FrameSerial == 18,
			"The first matching frame at the required revision issues the plan");
		if (control.m_Issued.size() != 1)
		{
			return;
		}
		control.Complete(control.m_Issued[0].m_Id);
		coordinator.Update();
		auto results = coordinatorJobs.TakeResults();
		const bool published = results.size() == 1 && results[0].m_JobId == waiting &&
			results[0].m_Status == CaptureJobStatus::Completed && results[0].m_FailureCode.empty() &&
			results[0].m_Products[0].m_Metadata && results[0].m_Products[0].m_Metadata->m_Control;
		context.Check(published, "A matching predicate capture publishes normally");
		if (published)
		{
			const FrameCaptureControlState& evidence = *results[0].m_Products[0].m_Metadata->m_Control;
			const std::string json = ReadText(ProductFile(results[0], ".json"));
			context.Check(evidence.m_HostViewControl.m_HostViewRevision == 3 && evidence.m_HostView.size() == 1 &&
				evidence.m_RequiredFrameState && evidence.m_RequiredFrameState->m_MinHostViewRevision == 3 &&
				json.find("\"control\": {\"schemaVersion\":1,\"hostViewControl\":") != std::string::npos &&
				json.find("\"minHostViewRevision\":3") != std::string::npos,
				"The sidecar records the issued frame's control evidence beside its predicate");
		}

		const uint64_t mismatched = coordinatorJobs.Admit(plan(MakeRequiredFrameState(false, 0)));
		coordinator.BeginFrame(MakeControlledFrameState(19, 4, true));
		coordinator.OnFrameSubmitted();
		coordinator.Update();
		results = coordinatorJobs.TakeResults();
		context.Check(control.m_Issued.size() == 1 && results.size() == 1 && results[0].m_JobId == mismatched &&
			results[0].m_Status == CaptureJobStatus::Failed &&
			results[0].m_FailureCode == "RequiredFrameStateMismatch" &&
			results[0].m_Products[0].m_Failure.find("view.taa.enabled value true, required false") != std::string::npos &&
			results[0].m_Products[0].m_Failure.find("temporal-aa status active, required disabled") != std::string::npos &&
			!results[0].m_Products[0].m_Metadata,
			"A contradicting due frame fails the plan with its differences and is never captured");

		auto sourced = MakeRequiredFrameState(true, 0);
		sourced->m_Values[0].m_Source = HostViewValueSource::Tooling;
		GGLAB_UNUSED(coordinatorJobs.Admit(plan(sourced)));
		coordinator.BeginFrame(MakeControlledFrameState(20, 4, true));
		coordinator.OnFrameSubmitted();
		coordinator.Update();
		results = coordinatorJobs.TakeResults();
		context.Check(results.size() == 1 && results[0].m_FailureCode == "RequiredFrameStateMismatch" &&
			results[0].m_Products[0].m_Failure.find("source host, required tooling") != std::string::npos,
			"A required source is compared with resolver attribution, not value equality");

		const uint64_t retried = coordinatorJobs.Admit(plan(MakeRequiredFrameState(true, 0)));
		coordinator.BeginFrame(MakeControlledFrameState(21, 5, true));
		const bool issued = control.m_Issued.size() == 2;
		if (issued)
		{
			control.Busy(control.m_Issued.back().m_Id);
			coordinator.Update();
		}
		coordinator.OnFrameAborted();
		coordinator.BeginFrame(MakeControlledFrameState(22, 6, false));
		coordinator.OnFrameSubmitted();
		coordinator.Update();
		results = coordinatorJobs.TakeResults();
		context.Check(issued && control.m_Issued.size() == 2 && results.size() == 1 &&
			results[0].m_JobId == retried && results[0].m_FailureCode == "RequiredFrameStateMismatch",
			"A retried attempt re-evaluates its predicate on the new frame");

		auto content = MakeRequiredFrameState(false, 0);
		content->m_ContentIdentity = 8;
		GGLAB_UNUSED(coordinatorJobs.Admit(plan(content)));
		coordinator.BeginFrame(MakeControlledFrameState(23, 6, false));
		coordinator.OnFrameSubmitted();
		coordinator.Update();
		results = coordinatorJobs.TakeResults();
		context.Check(results.size() == 1 && results[0].m_FailureCode == "RequiredFrameStateMismatch" &&
			results[0].m_Products[0].m_Failure.find("content identity 7, required 8") != std::string::npos,
			"A content activation other than the required one fails rather than waits");

		// A caller that keeps a mutable pointer cannot steer an admitted plan or its record.
		const auto shared = MakeRequiredFrameState(false, 0);
		const uint64_t frozen = coordinatorJobs.Admit(plan(shared));
		shared->m_Values[0].m_Value = true;
		shared->m_TemporalConsumers[0].m_Status = TemporalConsumerStatus::Active;
		coordinator.BeginFrame(MakeControlledFrameState(24, 6, false));
		coordinator.OnFrameSubmitted();
		const bool frozenIssued = control.m_Issued.size() == 3 && control.m_Issued.back().m_FrameSerial == 24;
		if (frozenIssued)
		{
			shared->m_MinHostViewRevision = 99;
			control.Complete(control.m_Issued.back().m_Id);
			coordinator.Update();
		}
		results = coordinatorJobs.TakeResults();
		const FrameCaptureMetadata* frozenMetadata =
			results.size() == 1 && results[0].m_JobId == frozen && results[0].m_Products[0].m_Metadata ?
				&*results[0].m_Products[0].m_Metadata :
				nullptr;
		const RequiredFrameState* recorded = frozenMetadata && frozenMetadata->m_Control ?
			frozenMetadata->m_Control->m_RequiredFrameState.get() :
			nullptr;
		context.Check(frozenIssued && recorded && results[0].m_Status == CaptureJobStatus::Completed &&
			recorded != shared.get() && !std::get<bool>(recorded->m_Values[0].m_Value) &&
			recorded->m_TemporalConsumers[0].m_Status == TemporalConsumerStatus::Disabled &&
			recorded->m_MinHostViewRevision == 0,
			"Admission keeps a private predicate snapshot for the issue decision and the sidecar");
	}

	void RunAfterReadyTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-ready");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({
			.m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(),
			.m_ImageEncoder = &EncodeTestPng,
			.m_WriteOnCallingThread = true,
		});
		CaptureTestJobs coordinatorJobs(coordinator, context);
		const uint64_t id = coordinatorJobs.Admit({
			.m_RequiredProducts = { ImageCaptureProduct{} },
			.m_Timing = FrameCaptureTiming::AfterReady,
			.m_SettleFrames = 2,
		});

		coordinator.BeginFrame(MakeFrameState(true));
		coordinator.OnFrameSubmitted();
		const bool waitingAfterOne = control.m_Issued.empty() &&
			coordinator.GetSettledFrameCount() == 1;
		// A new settle key, for example a temporal reset, restarts settling.
		coordinator.BeginFrame(MakeFrameState(true, 2));
		coordinator.OnFrameSubmitted();
		coordinator.BeginFrame(MakeFrameState(true, 2));
		const bool waitingAfterReset = control.m_Issued.empty();
		coordinator.OnFrameSubmitted();
		coordinator.BeginFrame(MakeFrameState(true, 2));
		context.Check(waitingAfterOne && waitingAfterReset && control.m_Issued.size() == 1,
			"After-ready captures wait for the requested settled frames of one settle key");

		// A frame that was begun but not submitted does not count as settled.
		coordinator.OnFrameSubmitted();
		coordinator.BeginFrame(MakeFrameState(true, 2));
		coordinator.BeginFrame(MakeFrameState(true, 2));
		context.Check(coordinator.GetSettledFrameCount() == 3,
			"Only submitted frames advance the settled-frame count");

		if (control.m_Issued.empty())
		{
			return;
		}
		control.Complete(control.m_Issued[0].m_Id);
		coordinator.Update();
		const std::vector<CaptureJobResult> results = coordinatorJobs.TakeResults();
		context.Check(results.size() == 1 && results[0].m_JobId == id &&
			results[0].m_Products.front().m_Metadata && results[0].m_Products.front().m_Metadata->m_SettledFrames == 2 &&
			results[0].m_Products.front().m_Metadata->m_SettleFrames == 2 &&
			results[0].m_Products.front().m_Metadata->m_Readiness.IsReady(),
			"After-ready metadata records the settled frames that preceded the capture");

		const uint64_t matching = coordinatorJobs.Admit({
			.m_RequiredProducts = { ImageCaptureProduct{} },
			.m_Timing = FrameCaptureTiming::AfterReady,
			.m_RequiredContentId = "gglab.lab.other",
		});
		const size_t issuedBefore = control.m_Issued.size();
		coordinator.BeginFrame(MakeFrameState(true, 2));
		const bool waitedForContent = control.m_Issued.size() == issuedBefore;
		coordinator.BeginFrame(MakeFrameState(true, 2, "gglab.lab.other"));
		context.Check(matching != 0 && waitedForContent &&
			control.m_Issued.size() == issuedBefore + 1,
			"A required content id holds the capture until that Demo or Lab is active");
	}

	void RunParameterSettlingTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("parameter-settling");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({ .m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
			.m_WriteOnCallingThread = true });
		CaptureTestJobs jobs(coordinator, context);
		auto state = MakeFrameState(true);
		state.m_ParameterRevision = 1;
		coordinator.BeginFrame(state);
		coordinator.OnFrameSubmitted();
		coordinator.BeginFrame(state);
		coordinator.OnFrameSubmitted();
		context.Check(coordinator.GetSettledFrameCount() == 2,
			"An unchanged parameter revision preserves settled submitted frames");
		GGLAB_UNUSED(jobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} },
			.m_Timing = FrameCaptureTiming::AfterReady, .m_SettleFrames = 2 }));
		++state.m_ParameterRevision;
		coordinator.BeginFrame(state);
		context.Check(control.m_Issued.empty() && coordinator.GetSettledFrameCount() == 0 &&
			!coordinator.ShouldRestartTemporalHistory(),
			"A new parameter revision restarts after-ready settling without resetting temporal history");
		coordinator.BeginFrame(state);
		context.Check(coordinator.GetSettledFrameCount() == 0,
			"An aborted parameter frame cannot contribute a settled sample");
		coordinator.OnFrameSubmitted();
		coordinator.BeginFrame(state);
		context.Check(control.m_Issued.empty() && coordinator.GetSettledFrameCount() == 1,
			"The capture waits for newly submitted frames of the changed parameter state");
		coordinator.OnFrameSubmitted();
		coordinator.BeginFrame(state);
		context.Check(control.m_Issued.size() == 1,
			"The capture issues only after its requested new-state settling count");
		if (!control.m_Issued.empty())
		{
			control.Complete(control.m_Issued.front().m_Id);
			coordinator.Update();
			GGLAB_UNUSED(jobs.TakeResults());
		}
	}

	void RunHistoryContinuitySettlingTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-continuity");
		for (uint32_t change = 0; change < 4; ++change)
		{
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(),
				.m_ImageEncoder = &EncodeTestPng, .m_WriteOnCallingThread = true });
			CaptureTestJobs coordinatorJobs(coordinator, context);
			FrameCaptureFrameState state = MakeFrameState(true);
			coordinator.BeginFrame(state);
			coordinator.OnFrameSubmitted();
			coordinator.BeginFrame(state);
			coordinator.OnFrameSubmitted();
			GGLAB_UNUSED(coordinatorJobs.Admit({
				.m_RequiredProducts = { ImageCaptureProduct{} },
				.m_Timing = FrameCaptureTiming::AfterReady,
				.m_SettleFrames = 2,
			}));
			TemporalFrameContinuity& continuity = state.m_SettleKey.m_Temporal;
			switch (change)
			{
			case 0:
				continuity.m_Identity.m_DepthExtent = { 43, 22 };
				break;
			case 1:
				continuity.m_Identity.m_Accumulation = TemporalAAHistoryAccumulation::CompatibilityAge;
				break;
			case 2:
				break; // Same identity, but the Runtime owner lost its history.
			case 3:
				continuity.m_Services = TemporalService::None;
				break;
			}
			continuity.m_HasCompatiblePreviousHistory = false;
			coordinator.BeginFrame(state);
			const bool restarted = control.m_Issued.empty() &&
				coordinator.GetSettledFrameCount() == 0;
			// Beginning another frame without a submission cannot advance settling.
			coordinator.BeginFrame(state);
			const bool notSubmitted = coordinator.GetSettledFrameCount() == 0;
			coordinator.OnFrameSubmitted();
			continuity.m_HasCompatiblePreviousHistory = true;
			coordinator.BeginFrame(state);
			const bool waited = control.m_Issued.empty() &&
				coordinator.GetSettledFrameCount() == 1;
			coordinator.OnFrameSubmitted();
			coordinator.BeginFrame(state);
			context.Check(restarted && notSubmitted && waited && control.m_Issued.size() == 1,
				std::format("Capture settling restarts for continuity change {} and counts "
					"only newly submitted frames", change));
			if (control.m_Issued.empty())
			{
				continue;
			}
			control.Complete(control.m_Issued.front().m_Id);
			coordinator.Update();
			const auto results = coordinatorJobs.TakeResults();
			context.Check(results.size() == 1 && results.front().m_Products.front().m_Metadata &&
				results.front().m_Products.front().m_Metadata->m_SettledFrames == 2,
				"Continuity recovery metadata counts only the new settled history");
		}
	}

	[[nodiscard]] FrameCaptureFrameState MakeCutFrameState(bool ready, uint64_t cameraResetSerial)
	{
		FrameCaptureFrameState state = MakeFrameState(ready);
		state.m_SettleKey.m_Temporal.m_Identity.m_ResetIdentity = cameraResetSerial;
		return state;
	}

	// Temporal history keeps accumulating while content loads. An after-ready
	// capture that settled over such history would depend on how many frames
	// loading took, so settling and time start at a camera cut on a ready frame.
	void RunLoadingHistoryTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-loading-history");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({
			.m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(),
			.m_ImageEncoder = &EncodeTestPng,
			.m_WriteOnCallingThread = true,
		});
		CaptureTestJobs coordinatorJobs(coordinator, context);
		const bool idleKeepsTime = !coordinator.ShouldHoldTime() &&
			!coordinator.ShouldRestartTemporalHistory();
		const uint64_t id = coordinatorJobs.Admit({
			.m_RequiredProducts = { ImageCaptureProduct{} },
			.m_Timing = FrameCaptureTiming::AfterReady,
			.m_SettleFrames = 1,
		});
		const bool holdsBeforeFirstFrame = coordinator.ShouldHoldTime();

		coordinator.BeginFrame(MakeCutFrameState(false, 1));
		coordinator.OnFrameSubmitted();
		const bool waitsWhileLoading = coordinator.ShouldHoldTime() &&
			!coordinator.ShouldRestartTemporalHistory();
		coordinator.BeginFrame(MakeCutFrameState(true, 1));
		coordinator.OnFrameSubmitted();
		coordinator.BeginFrame(MakeCutFrameState(true, 1));
		coordinator.OnFrameSubmitted();
		context.Check(idleKeepsTime && holdsBeforeFirstFrame && waitsWhileLoading &&
			control.m_Issued.empty() && coordinator.GetSettledFrameCount() == 0 &&
			coordinator.ShouldRestartTemporalHistory() && coordinator.ShouldHoldTime(),
			"Ready frames over history that began while loading do not settle; the "
			"coordinator holds time and asks for a temporal restart");

		// The runtime's camera cut on a ready frame starts settling and time.
		coordinator.BeginFrame(MakeCutFrameState(true, 2));
		coordinator.OnFrameSubmitted();
		const bool settlingFromCut = control.m_Issued.empty() &&
			coordinator.GetSettledFrameCount() == 1 &&
			!coordinator.ShouldRestartTemporalHistory() && !coordinator.ShouldHoldTime();
		coordinator.BeginFrame(MakeCutFrameState(true, 2));
		coordinator.OnFrameSubmitted();
		context.Check(id != 0 && settlingFromCut && control.m_Issued.size() == 1,
			"Settling and time start at the cut, and the capture follows the settled frames");

		// A gate leaving Ready puts loading frames into the history again.
		const uint64_t again = coordinatorJobs.Admit({
			.m_RequiredProducts = { ImageCaptureProduct{} },
			.m_Timing = FrameCaptureTiming::AfterReady,
			.m_SettleFrames = 1,
		});
		const bool settledHistoryKeepsTime = !coordinator.ShouldHoldTime() &&
			!coordinator.ShouldRestartTemporalHistory();
		coordinator.BeginFrame(MakeCutFrameState(false, 2));
		coordinator.OnFrameSubmitted();
		coordinator.BeginFrame(MakeCutFrameState(true, 2));
		coordinator.OnFrameSubmitted();
		context.Check(settledHistoryKeepsTime && control.m_Issued.size() == 1 &&
			coordinator.ShouldRestartTemporalHistory() && coordinator.ShouldHoldTime(),
			"Readiness lost under one settle key restarts temporal history once ready");

		// A next-frame capture never waits for history, so it neither holds time
		// nor cuts the view.
		const bool cancelled = (coordinator.CancelJob(again) == CaptureCancelResult::Cancelled);
		const uint64_t next = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		context.Check(cancelled && next != 0 && !coordinator.ShouldHoldTime() &&
			!coordinator.ShouldRestartTemporalHistory(),
			"Only waiting after-ready captures hold time or restart temporal history");
	}

	void RunReferenceViewTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-view");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({
			.m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(),
			.m_ImageEncoder = &EncodeTestPng,
			.m_WriteOnCallingThread = true,
		});
		CaptureTestJobs coordinatorJobs(coordinator, context);
		const uint64_t plain = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		const uint64_t viewA = coordinatorJobs.Admit({
			.m_RequiredProducts = { ImageCaptureProduct{} },
			.m_Timing = FrameCaptureTiming::AfterReady,
			.m_SettleFrames = 1,
			.m_ReferenceViewId = "CAM_A",
		});
		const uint64_t viewB = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} }, .m_ReferenceViewId = "CAM_B" });

		const bool noViewBeforeFrame = !coordinator.GetPendingViewChange();
		coordinator.BeginFrame(MakeViewFrameState(0));
		coordinator.OnFrameSubmitted();
		const std::optional<FrameCaptureViewChange> firstChange = coordinator.GetPendingViewChange();
		context.Check(noViewBeforeFrame && control.m_Issued.size() == 1 && firstChange &&
			firstChange->m_JobId == viewA && firstChange->m_ReferenceViewId == "CAM_A",
			"Reference views apply in submission order once earlier requests were issued");
		if (!firstChange)
		{
			return;
		}

		// Restoring the view is a camera cut: settling restarts under the view.
		coordinator.OnReferenceViewApplied(viewA, true);
		const bool appliedOnce = !coordinator.GetPendingViewChange();
		coordinator.BeginFrame(MakeViewFrameState(1));
		const bool waitedForSettle = control.m_Issued.size() == 1;
		coordinator.OnFrameSubmitted();
		coordinator.BeginFrame(MakeViewFrameState(1));
		coordinator.OnFrameSubmitted();
		context.Check(appliedOnce && waitedForSettle && control.m_Issued.size() == 2,
			"An after-ready view capture settles from the view's camera cut");

		const std::optional<FrameCaptureViewChange> secondChange = coordinator.GetPendingViewChange();
		context.Check(secondChange && secondChange->m_JobId == viewB,
			"The next view applies only after the previous view capture was issued");
		coordinator.OnReferenceViewApplied(viewB, false);
		control.Complete(control.m_Issued[0].m_Id);
		control.Complete(control.m_Issued[1].m_Id);
		coordinator.Update();
		const std::vector<CaptureJobResult> results = coordinatorJobs.TakeResults();
		const auto find = [&](uint64_t id) -> const CaptureJobResult*
		{
			const auto result = std::ranges::find(results, id, &CaptureJobResult::m_JobId);
			return result != results.end() ? &*result : nullptr;
		};
		const CaptureJobResult* plainResult = find(plain);
		const CaptureJobResult* viewAResult = find(viewA);
		const CaptureJobResult* viewBResult = find(viewB);
		context.Check(viewBResult && viewBResult->m_Status == CaptureJobStatus::Failed &&
			viewBResult->m_Products.front().m_Failure.find("'CAM_B'") != std::string::npos &&
			viewBResult->m_Products.front().m_Failure.find("available: CAM_A, CAM_B") != std::string::npos,
			"A view the content cannot restore fails with the available view ids");
		context.Check(plainResult && viewAResult && viewAResult->m_Products.front().m_Metadata &&
			plainResult->m_Products.front().m_Metadata && plainResult->m_Products.front().m_Metadata->m_Camera.m_ReferenceViewId.empty() &&
			viewAResult->m_Products.front().m_Metadata->m_Camera.m_ReferenceViewId == "CAM_A" &&
			!viewAResult->m_ManifestPath.empty() &&
			ReadText(ProductFile(*viewAResult, ".json")).find("\"referenceView\": \"CAM_A\"") != std::string::npos,
			"Published product metadata records the restored reference view");

		// Another camera cut before the capture replaces the view, so it is restored again.
		const uint64_t viewAgain = coordinatorJobs.Admit({
			.m_RequiredProducts = { ImageCaptureProduct{} },
			.m_Timing = FrameCaptureTiming::AfterReady,
			.m_SettleFrames = 2,
			.m_ReferenceViewId = "CAM_B",
		});
		const std::optional<FrameCaptureViewChange> againChange = coordinator.GetPendingViewChange();
		coordinator.OnReferenceViewApplied(viewAgain, true);
		coordinator.BeginFrame(MakeViewFrameState(5));
		coordinator.OnFrameSubmitted();
		const bool heldAfterApply = !coordinator.GetPendingViewChange();
		coordinator.BeginFrame(MakeViewFrameState(6));
		coordinator.OnFrameSubmitted();
		const std::optional<FrameCaptureViewChange> reapply = coordinator.GetPendingViewChange();
		context.Check(againChange && heldAfterApply && reapply &&
			reapply->m_JobId == viewAgain && (coordinator.CancelJob(viewAgain) == CaptureCancelResult::Cancelled),
			"A camera cut that replaces an applied view restores the view again");
	}

	void RunFailureTests(SelfTestContext& context) noexcept
	{
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({
			.m_Capture = &control,
			.m_ImageEncoder = &EncodeTestPng,
			.m_WriteOnCallingThread = true,
		});
		CaptureTestJobs coordinatorJobs(coordinator, context);

		const uint64_t waiting = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} }, .m_Timing = FrameCaptureTiming::AfterReady });
		const uint64_t failing = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		coordinator.BeginFrame(MakeFrameState(false));
		const bool cancelled = (coordinator.CancelJob(waiting) == CaptureCancelResult::Cancelled);
		const bool issued = coordinator.GetJobSnapshot(failing)->m_Stage == CaptureJobStage::AwaitingProducts;
		if (!control.m_Issued.empty())
		{
			control.Fail(control.m_Issued[0].m_Id, "Device lost.");
		}
		coordinator.Update();
		const std::vector<CaptureJobResult> results = coordinatorJobs.TakeResults();
		context.Check(cancelled && issued && results.size() == 2 &&
			results[0].m_JobId == waiting &&
			results[0].m_Status == CaptureJobStatus::Cancelled &&
			!results[0].m_Products.front().m_Metadata && results[1].m_JobId == failing &&
			results[1].m_Status == CaptureJobStatus::Failed &&
			results[1].m_Products.front().m_Failure == "Device lost." && results[1].m_Products.front().m_Metadata,
			"Waiting requests cancel without metadata; Runtime failures keep their reason");

		const uint64_t blocked = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} }, .m_Timing = FrameCaptureTiming::AfterReady });
		const uint64_t otherContent = coordinatorJobs.Admit({
			.m_RequiredProducts = { ImageCaptureProduct{} },
			.m_Timing = FrameCaptureTiming::AfterReady,
			.m_RequiredContentId = "gglab.lab.other",
		});
		FrameCaptureFrameState failedState = MakeFrameState(true);
		failedState.m_Readiness.Add("lab", FrameCaptureGateState::Failed, "Session creation failed.");
		coordinator.BeginFrame(failedState);
		const std::vector<CaptureJobResult> failedGate = coordinatorJobs.TakeResults();
		const bool otherContentWaiting = (coordinator.CancelJob(otherContent) == CaptureCancelResult::Cancelled);
		const std::vector<CaptureJobResult> cancelledOther = coordinatorJobs.TakeResults();
		context.Check(failedGate.size() == 1 && failedGate[0].m_JobId == blocked &&
			failedGate[0].m_Status == CaptureJobStatus::Failed &&
			failedGate[0].m_Products.front().m_Failure == "Readiness failed: lab (Session creation failed.)." &&
			otherContentWaiting && cancelledOther.size() == 1,
			"A failed gate fails waiting after-ready captures of that content at once");

		TemporaryDirectory directory("frame-capture-no-encoder");
		FrameCaptureCoordinator withoutEncoder({
			.m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(),
			.m_WriteOnCallingThread = true,
		});
		CaptureTestJobs withoutEncoderJobs(withoutEncoder, context);
		const uint64_t notEncoded = withoutEncoderJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		withoutEncoder.BeginFrame(MakeFrameState(true));
		control.Complete(control.m_Issued.back().m_Id);
		withoutEncoder.Update();
		const std::vector<CaptureJobResult> noEncoder = withoutEncoderJobs.TakeResults();
		context.Check(noEncoder.size() == 1 && noEncoder[0].m_JobId == notEncoded &&
			noEncoder[0].m_Status == CaptureJobStatus::Failed &&
			noEncoder[0].m_Products.front().m_Failure == "No capture image encoder is configured." &&
			!std::filesystem::exists(directory.GetPath()),
			"Without a host image encoder captures fail instead of writing files");

		const uint64_t noDirectory = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		coordinator.BeginFrame(MakeFrameState(true));
		control.Complete(control.m_Issued.back().m_Id);
		coordinator.Update();
		const std::vector<CaptureJobResult> unwritable = coordinatorJobs.TakeResults();
		context.Check(unwritable.size() == 1 && unwritable[0].m_JobId == noDirectory &&
			unwritable[0].m_Status == CaptureJobStatus::Failed &&
			ProductFile(unwritable[0], ".png").empty(),
			"A capture without an output directory fails instead of guessing a location");
	}

	void RunShutdownTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-shutdown");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({
			.m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(),
			.m_ImageEncoder = &EncodeTestPng,
			.m_WriteOnCallingThread = true,
		});
		CaptureTestJobs coordinatorJobs(coordinator, context);
		const uint64_t waiting = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} }, .m_Timing = FrameCaptureTiming::AfterReady });
		const uint64_t completed = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		const uint64_t lost = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{ CaptureImageSource::Composited } } });
		coordinator.BeginFrame(MakeFrameState(false));

		coordinator.PrepareForShutdown();
		const auto late = coordinator.TrySubmit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		// The render host finalizes: one capture completed, the other lost.
		control.Complete(control.m_Issued[0].m_Id);
		coordinator.FinalizeAfterRenderHost();
		const std::vector<CaptureJobResult> results = coordinatorJobs.TakeResults();

		const auto find = [&](uint64_t id) -> const CaptureJobResult*
		{
			for (const CaptureJobResult& result : results)
			{
				if (result.m_JobId == id)
				{
					return &result;
				}
			}
			return nullptr;
		};
		const CaptureJobResult* waitingResult = find(waiting);
		const CaptureJobResult* completedResult = find(completed);
		const CaptureJobResult* lostResult = find(lost);
		context.Check(results.size() == 3 && waitingResult && completedResult && lostResult &&
			late.m_Status == CaptureAdmissionStatus::Rejected && late.m_JobId == 0 &&
			waitingResult->m_Status == CaptureJobStatus::Cancelled &&
			completedResult->m_Status == CaptureJobStatus::Completed &&
			std::filesystem::exists(ProductFile(*completedResult, ".png")) &&
			lostResult->m_Status == CaptureJobStatus::Failed &&
			coordinator.GetUnfinishedRequestCount() == 0,
			"Shutdown settles accepted jobs once, encodes completed work and rejects new intake without a receipt");
	}

	void RunWatchShutdownTests(SelfTestContext& context) noexcept
	{
		for (const auto mode : { NonFiniteWatchMode::Report, NonFiniteWatchMode::Fail })
		{
			for (const auto claim : { NonFiniteClaimStatus::Passed, NonFiniteClaimStatus::Failed, NonFiniteClaimStatus::Incomplete })
			{
				TemporaryDirectory directory("frame-capture-watch-shutdown");
				FakeCaptureControl control;
				control.m_Instrumentation = true;
				NonFiniteWatchCoordinator watch(&control);
				std::string error;
				context.Check(watch.Configure({ .m_Mode = mode, .m_Boundary = false }, error),
					"Shutdown watch configures a production coverage range");
				FrameCaptureCoordinator coordinator({
					.m_Capture = &control,
					.m_DefaultOutputDirectory = directory.GetPath(),
					.m_ImageEncoder = &EncodeTestPng,
					.m_WriteOnCallingThread = true,
					.m_NonFiniteWatch = &watch,
				});
				CaptureTestJobs coordinatorJobs(coordinator, context);
				const auto id = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
				const auto frame = MakeFrameState(false);
				coordinator.BeginFrame(frame);
				watch.OnFrameSubmitted(frame.m_FrameSerial);
				coordinator.OnFrameSubmitted();
				coordinator.PrepareForShutdown();

				// The last fence completes during render-host finalization, after the
				// coordinator's last ordinary poll. Missing evidence has no queued frame.
				control.Complete(control.m_Issued[0].m_Id);
				if (claim != NonFiniteClaimStatus::Incomplete)
				{
					auto production = std::make_shared<NonFiniteProductionFrame>();
					production->m_FrameSerial = frame.m_FrameSerial;
					production->m_Claim = { claim, NonFiniteWatchFailure::None };
					if (claim == NonFiniteClaimStatus::Failed)
					{
						production->m_Claim.m_Failure = NonFiniteWatchFailure::NonFiniteReplaced;
						production->m_Sites.push_back({ .m_ProgramIndex = 2, .m_SiteIndex = 3, .m_Claim = production->m_Claim });
					}
					control.m_Production.push_back(std::move(production));
				}
				coordinator.FinalizeAfterRenderHost();
				const auto results = coordinatorJobs.TakeResults();
				const bool publishes = mode == NonFiniteWatchMode::Report || claim == NonFiniteClaimStatus::Passed;
				context.Check(results.size() == 1 && results[0].m_JobId == id && results[0].m_Products.front().m_Metadata &&
					results[0].m_Products.front().m_Metadata->m_WatchCoverage && results[0].m_Products.front().m_Metadata->m_WatchCoverage->m_Complete &&
					results[0].m_Products.front().m_Metadata->m_WatchCoverage->m_CompletedFrames == 1 &&
					results[0].m_Products.front().m_Metadata->m_WatchCoverage->m_Production.m_Status == claim &&
					results[0].m_Status == (publishes ? CaptureJobStatus::Completed : CaptureJobStatus::Failed) &&
					(publishes ?
						std::filesystem::exists(ProductFile(results[0], ".png")) :
						ProductFile(results[0], ".png").empty() && results[0].m_Products.front().m_Failure.starts_with(
							claim == NonFiniteClaimStatus::Failed ? "NonFiniteReplaced" : "MissingCoverage")) &&
					coordinator.GetUnfinishedRequestCount() == 0,
					"Shutdown drains final production evidence before adjudicating only genuinely missing coverage");
			}
		}
	}

	void RunDiagnosticCaptureTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-diagnostic");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({
			.m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(),
			.m_ImageEncoder = &EncodeTestPng,
			.m_WriteOnCallingThread = true,
		});
		CaptureTestJobs coordinatorJobs(coordinator, context);
		const auto missingTap = coordinator.TrySubmit({ .m_RequiredProducts = { DiagnosticCaptureProduct{} } });
		const uint64_t weight = coordinatorJobs.Admit({
			.m_RequiredProducts = { DiagnosticCaptureProduct{ PostProcessDebugTap::TemporalHistoryWeight } },
		});
		coordinator.BeginFrame(MakeFrameState(true));
		coordinator.OnFrameSubmitted();
		if (!control.m_Issued.empty())
		{
			control.Complete(control.m_Issued.back().m_Id);
		}
		coordinator.Update();
		const std::vector<CaptureJobResult> results = coordinatorJobs.TakeResults();
		const auto find = [&](uint64_t id)
		{
			return std::ranges::find(results, id, &CaptureJobResult::m_JobId);
		};
		const auto completed = find(weight);
		context.Check(control.m_Issued.size() == 1 &&
			control.m_Issued[0].m_Source == FrameCaptureSource::Diagnostic &&
			control.m_Issued[0].m_DiagnosticTap == PostProcessDebugTap::TemporalHistoryWeight &&
			missingTap.m_Status == CaptureAdmissionStatus::Rejected && missingTap.m_JobId == 0 &&
			results.size() == 1 &&
			completed != results.end() &&
			completed->m_Status == CaptureJobStatus::Completed &&
			completed->m_Products.front().m_Metadata &&
			completed->m_Products.front().m_Metadata->m_DiagnosticTap == "temporal-history-weight",
			"Diagnostic captures pass their tap to the Runtime, record its name and fail "
			"without a tap");

		SequenceHarness harness(directory.GetPath());
		std::string mismatch;
		std::string ok;
		const uint64_t rejectedSequence = harness.m_Sequence.Start({
			.m_CameraPathId = "SEQ_Test",
			.m_CaptureSource = FrameCaptureSource::Diagnostic,
			}, mismatch);
		const uint64_t sequence = harness.m_Sequence.Start({
			.m_CameraPathId = "SEQ_Test",
			.m_CaptureFrames = { 2 },
			.m_CaptureSource = FrameCaptureSource::Diagnostic,
			.m_DiagnosticTap = PostProcessDebugTap::TemporalRejection,
			}, ok);
		while (harness.m_Sequence.IsActive())
		{
			harness.Frame();
		}
		context.Check(rejectedSequence == 0 && !mismatch.empty() && sequence != 0 &&
			harness.m_Control.m_Issued.size() == 1 &&
			harness.m_Control.m_Issued[0].m_DiagnosticTap == PostProcessDebugTap::TemporalRejection &&
			harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Completed,
			"A sequence records one diagnostic tap as its evidence channel");
	}

	void RunDiagnosticSchedulingTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-binding");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({
			.m_Capture = &control, .m_DefaultOutputDirectory = directory.GetPath(),
			.m_ImageEncoder = &EncodeTestPng, .m_WriteOnCallingThread = true,
		});
		CaptureTestJobs coordinatorJobs(coordinator, context);
		const auto weight = PostProcessDebugTap::TemporalHistoryWeight;
		const auto rejection = PostProcessDebugTap::TemporalRejection;
		const uint64_t first = coordinatorJobs.Admit({
			.m_RequiredProducts = { DiagnosticCaptureProduct{ weight } },
		});
		const uint64_t second = coordinatorJobs.Admit({
			.m_RequiredProducts = { DiagnosticCaptureProduct{ rejection } },
		});
		GGLAB_UNUSED(coordinatorJobs.Admit({
			.m_RequiredProducts = { DiagnosticCaptureProduct{ weight } },
		}));
		GGLAB_UNUSED(coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } }));
		GGLAB_UNUSED(coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{ CaptureImageSource::Composited } } }));
		const uint64_t cancel = coordinatorJobs.Admit({
			.m_RequiredProducts = { DiagnosticCaptureProduct{ PostProcessDebugTap::TemporalClipDistance } },
		});
		FrameCaptureFrameState state = MakeFrameState(true);
		state.m_FrameSerial = 100;
		coordinator.BeginFrame(state);
		coordinator.OnFrameSubmitted();
		context.Check(control.m_Issued.size() == 4 &&
			std::ranges::all_of(control.m_Issued, [&](const auto& issued)
				{
					return issued.m_FrameSerial == 100 &&
						(!issued.m_DiagnosticTap || issued.m_DiagnosticTap == weight);
				}), "One frame coalesces matching diagnostics alongside scene and composited captures");
		context.Check((coordinator.CancelJob(cancel) == CaptureCancelResult::Cancelled), "A conflicting tap stays cancellable before issue");
		state.m_FrameSerial = 101;
		state.m_FrameIndex = 43;
		state.m_TotalTime = 0.75;
		state.m_Camera.m_Position[0] = 9.0f;
		state.m_Temporal.m_JitterIndex = 5;
		coordinator.BeginFrame(state);
		coordinator.OnFrameSubmitted();
		context.Check(control.m_Issued.size() == 5 &&
			control.m_Issued.back().m_DiagnosticTap == rejection &&
			control.m_Issued.back().m_FrameSerial == 101,
			"A different diagnostic tap is issued with the next frame's identity");
		for (const auto& issued : control.m_Issued)
		{
			control.Complete(issued.m_Id);
		}
		coordinator.Update();
		const auto results = coordinatorJobs.TakeResults();
		const auto a = std::ranges::find(results, first, &CaptureJobResult::m_JobId);
		const auto b = std::ranges::find(results, second, &CaptureJobResult::m_JobId);
		context.Check(a != results.end() && b != results.end() && a->m_Products.front().m_Metadata && b->m_Products.front().m_Metadata &&
			a->m_Status == CaptureJobStatus::Completed &&
			b->m_Status == CaptureJobStatus::Completed &&
			a->m_Products.front().m_Metadata->m_FrameSerial == 100 && a->m_Products.front().m_Metadata->m_FrameIndex == 42 &&
			a->m_Products.front().m_Metadata->m_TotalTime == 0.5 && a->m_Products.front().m_Metadata->m_Camera.m_Position[0] == 1.0f &&
			b->m_Products.front().m_Metadata->m_FrameSerial == 101 && b->m_Products.front().m_Metadata->m_FrameIndex == 43 &&
			b->m_Products.front().m_Metadata->m_TotalTime == 0.75 && b->m_Products.front().m_Metadata->m_Camera.m_Position[0] == 9.0f &&
			b->m_Products.front().m_Metadata->m_Temporal.m_JitterIndex == 5,
			"Delayed GPU completions keep frame metadata immutable; a deferred tap uses its actual issue frame");

		SequenceHarness harness(directory.GetPath());
		std::string failure;
		GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
			.m_CaptureFrames = { 0 }, .m_CaptureSource = FrameCaptureSource::Diagnostic,
			.m_DiagnosticTap = rejection }, failure));
		harness.Frame();
		CaptureTestJobs externalJobs(harness.m_Capture, context);
		GGLAB_UNUSED(externalJobs.Admit({
			.m_RequiredProducts = { DiagnosticCaptureProduct{ weight } },
		}));
		harness.Frame();
		context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
			harness.m_Control.m_Issued.size() == 1 &&
			harness.m_Control.m_Issued.front().m_DiagnosticTap == weight,
			"A conflicting sequence capture fails on its required frame and never issues a later substitute");
	}

	void RunAttemptRetryTests(SelfTestContext& context) noexcept
	{
		for (const auto& product : std::vector<CaptureProductSpec>{ ImageCaptureProduct{}, DiagnosticCaptureProduct{ PostProcessDebugTap::TemporalHistoryWeight }, GraphCaptureProduct{}, ResourceCaptureProduct{ { { .m_Resource = "A" } } } })
		{
			const auto source = GetCaptureProductSource(product);
			TemporaryDirectory directory("frame-capture-attempt-retry");
			FakeCaptureControl control;
			control.m_Instrumentation = true;
			NonFiniteWatchCoordinator watch(&control);
			std::string error;
			context.Check(watch.Configure({ .m_Mode = NonFiniteWatchMode::Fail,
				.m_Boundary = false }, error), "Attempt retry configures production coverage");
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
				.m_WriteOnCallingThread = true, .m_NonFiniteWatch = &watch });
			CaptureTestJobs coordinatorJobs(coordinator, context);
			CapturePlan plan{
				.m_RequiredProducts = { product },
				.m_Label = "retry",
				.m_Note = "Keep the logical request across attempts.",
			};
			if (source == FrameCaptureSource::ResourceExport)
			{
				plan.m_RequiredProducts.emplace_back(GraphCaptureProduct{});
			}
			const auto id = coordinatorJobs.Admit(std::move(plan));
			auto state = MakeFrameState(true);
			for (uint32_t retry = 0; retry < 2; ++retry)
			{
				coordinator.BeginFrame(state);
				coordinator.SetFrameSceneInputs(state.m_FrameSerial,
					{ .m_LocalLightCount = retry + 1 });
				control.Busy(control.m_Issued.back().m_Id);
				coordinator.Update();
				context.Check(coordinatorJobs.TakeResults().empty() &&
					coordinator.GetUnfinishedRequestCount() == 1 &&
					!std::filesystem::exists(directory.GetPath()),
					"Busy retires an attempt without completing or publishing its logical job");
				++state.m_FrameSerial;
				++state.m_FrameIndex;
				state.m_TotalTime += 1.0;
				state.m_Camera.m_Name = "Retried camera";
			}
			coordinator.BeginFrame(state);
			coordinator.SetFrameSceneInputs(state.m_FrameSerial, { .m_LocalLightCount = 99 });
			watch.OnFrameSubmitted(state.m_FrameSerial);
			coordinator.OnFrameSubmitted();
			control.Complete(control.m_Issued.front().m_Id);
			coordinator.Update();
			context.Check(control.m_Issued.size() == 3 && coordinatorJobs.TakeResults().empty() &&
				coordinator.GetUnfinishedRequestCount() == 1,
				"A stale Runtime result cannot complete a newer attempt of the same job");

			control.Complete(control.m_Issued.back().m_Id);
			coordinator.Update();
			context.Check(coordinatorJobs.TakeResults().empty() &&
				!std::filesystem::exists(directory.GetPath()),
				"The retried product still waits for its own submitted-frame coverage");
			auto production = std::make_shared<NonFiniteProductionFrame>();
			production->m_FrameSerial = state.m_FrameSerial;
			production->m_Claim = { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None };
			control.m_Production.push_back(std::move(production));
			coordinator.Update();
			const auto completed = coordinatorJobs.TakeResults();
			context.Check(completed.size() == 1 && completed[0].m_JobId == id &&
				completed[0].m_Status == CaptureJobStatus::Completed &&
				completed[0].m_Products.front().m_Metadata && completed[0].m_Products.front().m_Metadata->m_Source == source &&
				completed[0].m_Products.front().m_Metadata->m_FrameSerial == state.m_FrameSerial &&
				completed[0].m_Products.front().m_Metadata->m_FrameIndex == state.m_FrameIndex &&
				completed[0].m_Products.front().m_Metadata->m_TotalTime == state.m_TotalTime &&
				completed[0].m_Products.front().m_Metadata->m_Camera.m_Name == state.m_Camera.m_Name &&
				completed[0].m_Products.front().m_Metadata->m_SceneInputs &&
				completed[0].m_Products.front().m_Metadata->m_SceneInputs->m_LocalLightCount == 99 &&
				completed[0].m_Products.front().m_Metadata->m_WatchCoverage &&
				completed[0].m_Products.front().m_Metadata->m_WatchCoverage->m_Complete &&
				completed[0].m_Products.front().m_Metadata->m_WatchCoverage->m_FrameCount == 1 &&
				completed[0].m_Products.front().m_Metadata->m_WatchCoverage->m_FirstFrameSerial == state.m_FrameSerial &&
				completed[0].m_Products.front().m_Metadata->m_WatchCoverage->m_LastFrameSerial == state.m_FrameSerial,
				"One logical result uses only the final attempt's provenance and coverage");
			control.Complete(control.m_Issued.back().m_Id);
			coordinator.Update();
			context.Check(coordinatorJobs.TakeResults().empty() &&
				coordinator.GetUnfinishedRequestCount() == 0,
				"A duplicate late result cannot publish or complete the job twice");
		}

		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({ .m_Capture = &control });
		CaptureTestJobs coordinatorJobs(coordinator, context);
		const auto id = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		coordinator.BeginFrame(MakeFrameState(true));
		control.Busy(control.m_Issued.back().m_Id);
		coordinator.Update();
		context.Check((coordinator.CancelJob(id) == CaptureCancelResult::Cancelled), "A job returned from Busy remains cancellable");
		const auto cancelled = coordinatorJobs.TakeResults();
		context.Check(cancelled.size() == 1 && cancelled[0].m_JobId == id &&
			cancelled[0].m_Status == CaptureJobStatus::Cancelled &&
			!cancelled[0].m_Products.front().m_Metadata && coordinator.GetUnfinishedRequestCount() == 0,
			"Cancelling after Busy publishes no provenance from the discarded attempt");
	}

	void RunFrameResultIdentityTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-result-identity");
		for (uint32_t mismatch = 0; mismatch < 3; ++mismatch)
		{
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({
				.m_Capture = &control, .m_DefaultOutputDirectory = directory.GetPath(),
				.m_ImageEncoder = &EncodeTestPng, .m_WriteOnCallingThread = true,
			});
			CaptureTestJobs coordinatorJobs(coordinator, context);
			GGLAB_UNUSED(coordinatorJobs.Admit({
				.m_RequiredProducts = { DiagnosticCaptureProduct{ PostProcessDebugTap::TemporalHistoryWeight } },
			}));
			coordinator.BeginFrame(MakeFrameState(true));
			if (mismatch == 1)
			{
				control.m_Issued.front().m_Source = FrameCaptureSource::Scene;
			}
			if (mismatch == 2)
			{
				control.m_Issued.front().m_DiagnosticTap = PostProcessDebugTap::TemporalRejection;
			}
			control.Complete(control.m_Issued.front().m_Id, mismatch == 0 ? 18u : 17u);
			coordinator.Update();
			const auto results = coordinatorJobs.TakeResults();
			context.Check(results.size() == 1 && results[0].m_Status == CaptureJobStatus::Failed &&
				results[0].m_Products.front().m_Metadata && results[0].m_Products.front().m_Metadata->m_FrameSerial == 17 &&
				ProductFile(results[0], ".png").empty() && !std::filesystem::exists(directory.GetPath()),
				std::format("A result with mismatched identity component {} never publishes a PNG", mismatch));
		}
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({ .m_Capture = &control });
		CaptureTestJobs coordinatorJobs(coordinator, context);
		control.m_PendingDiagnosticTap = PostProcessDebugTap::TemporalHistoryWeight;
		const uint64_t waiting = coordinatorJobs.Admit({
			.m_RequiredProducts = { DiagnosticCaptureProduct{ PostProcessDebugTap::TemporalRejection } },
		});
		coordinator.BeginFrame(MakeFrameState(true));
		context.Check(control.m_Issued.empty() && (coordinator.CancelJob(waiting) == CaptureCancelResult::Cancelled),
			"Scheduling respects a diagnostic tap already queued by another control client");
		GGLAB_UNUSED(coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } }));
		auto noFrame = MakeFrameState(true);
		noFrame.m_FrameSerial = 0;
		coordinator.BeginFrame(noFrame);
		const auto failed = coordinatorJobs.TakeResults();
		context.Check(control.m_Issued.empty() && std::ranges::any_of(failed, [](const auto& result)
			{
				return result.m_Status == CaptureJobStatus::Failed;
			}), "The coordinator cannot accidentally issue a flexible request without a render serial");
	}
}

namespace gglab
{
	using namespace capture_test;

	void RunCaptureSelectionSelfTests(SelfTestContext& context) noexcept
	{
		RunNextFrameTests(context);
		RunRequiredFrameStateTests(context);
		RunFrameSelectionDeadlineTests(context);
		RunAfterReadyTests(context);
		RunParameterSettlingTests(context);
		RunHistoryContinuitySettlingTests(context);
		RunLoadingHistoryTests(context);
		RunReferenceViewTests(context);
		RunFailureTests(context);
		RunShutdownTests(context);
		RunWatchShutdownTests(context);
		RunDiagnosticCaptureTests(context);
		RunDiagnosticSchedulingTests(context);
		RunFrameResultIdentityTests(context);
		RunAttemptRetryTests(context);
	}
}
