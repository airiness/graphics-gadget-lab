#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "CaptureCoordinatorTestCases.h"
#include "CaptureTestServices.h"
#include "FrameCaptureCoordinatorSelfTests.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAOTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGObservation.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabRuntime/Graphics/ViewRenderSettingsPatch.h"
#include "GGLabTestCore/SelfTest.h"
#include "Lab/LabParameter.h"
#include "SequenceTestServices.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <string>
#include <system_error>
#include <vector>

namespace gglab::capture_test
{
	void RunFrameSequenceTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-sequence");
		SequenceHarness harness(directory.GetPath());
		std::string error;
		const uint64_t id = harness.m_Sequence.Start({
			.m_CameraPathId = "SEQ_Test",
			.m_CaptureFrames = { 5, 1, 1 },
			.m_Label = "run",
			}, error);
		std::string busyError;
		context.Check(id != 0 && harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test" },
			busyError) == 0 && !busyError.empty(),
			"Only one sequence runs at a time");

		harness.m_Ready = false;
		const std::optional<uint32_t> beforeFrames = harness.Frame();
		const std::optional<uint32_t> notReady = harness.Frame();
		harness.m_Ready = true;
		const std::optional<uint32_t> readyPrevious = harness.Frame();
		context.Check(!beforeFrames && !notReady && !readyPrevious &&
			harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Waiting,
			"A sequence waits until a rendered frame reports every readiness gate ready");

		std::vector<uint32_t> posed;
		std::vector<size_t> issuedAfterFrame;
		for (uint32_t frame = 0; frame < 6; ++frame)
		{
			if (frame == 2)
			{
				// A frame that ends without submission is posed again.
				const std::optional<uint32_t> skipped = harness.Frame(false);
				context.Check(skipped == 2u, "An unsubmitted frame keeps its sequence frame");
			}
			const std::optional<uint32_t> pose = harness.Frame();
			posed.push_back(pose.value_or(999));
			issuedAfterFrame.push_back(harness.m_Control.m_Issued.size());
		}
		const FrameSequenceStatus& status = *harness.m_Sequence.GetStatus();
		context.Check(posed == std::vector<uint32_t>{ 0, 1, 2, 3, 4, 5 } &&
			status.m_State == FrameSequenceState::Completed && status.m_FrameCount == 6 &&
			status.m_CameraPathVersion == 2 && status.m_SubmittedFrames == 6 &&
			status.m_CompletedCaptures == 2,
			"Each submitted frame advances the path by one frame until the sequence completes");
		context.Check(issuedAfterFrame == std::vector<size_t>{ 0, 1, 1, 1, 1, 2 },
			"Requested frames are captured on exactly that frame, once, with duplicates ignored");
		bool labelled = false;
		std::error_code errorCode;
		for (const auto& entry : std::filesystem::directory_iterator(directory.GetPath(), errorCode))
		{
			labelled |= entry.path().filename().string().starts_with("run-f0005.");
		}
		context.Check(labelled, "Sequence captures are labelled with their sequence frame");
		const auto lastCapture = std::ranges::find_if(harness.m_Results,
			[](const CaptureProductResult& result)
			{
				return result.m_Metadata && result.m_Metadata->m_Sequence &&
					result.m_Metadata->m_Sequence->m_Frame == 5;
			});
		context.Check(lastCapture != harness.m_Results.end() &&
			lastCapture->m_Metadata->m_Sequence->m_CameraPathId == "SEQ_Test" &&
			lastCapture->m_Metadata->m_Sequence->m_CameraPathVersion == 2 &&
			lastCapture->m_Metadata->m_Sequence->m_FrameCount == 6 &&
			lastCapture->m_Metadata->m_Sequence->m_SequenceId == id,
			"Sequence capture metadata names the sequence, path version and frame");
		context.Check(!harness.Frame(), "A completed sequence no longer poses frames");
	}

	void RunFrameSequenceBackPressureTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-sequence-backpressure");
		SequenceHarness harness(directory.GetPath());
		harness.m_AutoComplete = false;
		std::string error;
		GGLAB_UNUSED(harness.m_Sequence.Start({
			.m_CameraPathId = "SEQ_Test",
			.m_CaptureFrames = { 0, 1, 2, 3, 4, 5 },
			}, error));
		harness.Frame();
		// Six frames issue six captures whose Runtime results are still pending.
		for (uint32_t frame = 0; frame < 6; ++frame)
		{
			harness.Frame();
		}
		context.Check(harness.m_Control.m_Issued.size() == 6 && !harness.m_Deferred &&
			harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Finishing,
			"Captures below the writer limit never defer a frame");

		SequenceHarness limited(directory.GetPath());
		limited.m_AutoComplete = false;
		limited.m_Paths.front().m_Keys.back().m_Frame = 20;
		std::vector<uint32_t> frames(21);
		for (uint32_t frame = 0; frame < frames.size(); ++frame)
		{
			frames[frame] = frame;
		}
		GGLAB_UNUSED(limited.m_Sequence.Start(
			{ .m_CameraPathId = "SEQ_Test", .m_CaptureFrames = frames }, error));
		limited.Frame();
		for (uint32_t frame = 0; frame < FrameCaptureCoordinator::MaxInFlightCaptures; ++frame)
		{
			limited.Frame();
		}
		const size_t issuedAtLimit = limited.m_Control.m_Issued.size();
		const std::optional<uint32_t> deferred = limited.Frame();
		const bool deferredAgain = !limited.Frame() && limited.m_Deferred;
		context.Check(issuedAtLimit == FrameCaptureCoordinator::MaxInFlightCaptures &&
			!deferred && deferredAgain &&
			limited.m_Control.m_Issued.size() == issuedAtLimit &&
			limited.m_Sequence.GetStatus()->m_SubmittedFrames == FrameCaptureCoordinator::MaxInFlightCaptures,
			"A capture beyond the writer limit defers the frame without posing or submitting it");

		limited.m_AutoComplete = true;
		limited.m_Control.Complete(limited.m_Control.m_Issued[0].m_Id);
		limited.m_Completed = 1;
		// The next tick is still deferred; it drains the finished capture.
		const std::optional<uint32_t> draining = limited.Frame();
		const std::optional<uint32_t> resumed = limited.Frame();
		context.Check(!draining && resumed == FrameCaptureCoordinator::MaxInFlightCaptures,
			"The sequence resumes with the deferred frame once the writer drains");
		while (limited.m_Sequence.IsActive())
		{
			limited.Frame();
		}
		const FrameSequenceStatus& status = *limited.m_Sequence.GetStatus();
		context.Check(status.m_State == FrameSequenceState::Completed &&
			status.m_CompletedCaptures == 21 && status.m_SubmittedFrames == 21,
			"Every frame of a fully captured sequence is captured without writer overflow");

		SequenceHarness payloadLimited(directory.GetPath());
		GGLAB_UNUSED(payloadLimited.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
			.m_CaptureFrames = { 0 }, .m_ReferenceSamples = 3 }, error));
		payloadLimited.Frame();
		payloadLimited.Frame(); // Reference sample zero.
		payloadLimited.m_Control.m_Availability = FrameCaptureAvailability::Busy;
		payloadLimited.Frame(); // Sample one does not need capture capacity yet.
		const bool held = !payloadLimited.Frame() && payloadLimited.m_Deferred &&
			!payloadLimited.Frame() && payloadLimited.m_Deferred;
		context.Check(held && payloadLimited.m_Control.m_Issued.empty() &&
			payloadLimited.m_LastSample && payloadLimited.m_LastSample->m_Index == 1 &&
			payloadLimited.m_Sequence.GetStatus()->m_SubmittedFrames == 0,
			"Payload pressure holds the final reference sample without posing or advancing the captured frame");
		payloadLimited.m_Control.m_Availability = FrameCaptureAvailability::Available;
		const auto finalSample = payloadLimited.Frame();
		context.Check(finalSample == 0u && payloadLimited.m_LastSample &&
			payloadLimited.m_LastSample->m_Index == 2 && payloadLimited.m_Results.size() == 1 &&
			payloadLimited.m_Results[0].m_Metadata &&
			payloadLimited.m_Results[0].m_Metadata->m_Sequence &&
			payloadLimited.m_Results[0].m_Metadata->m_Sequence->m_Frame == 0 &&
			payloadLimited.m_Sequence.GetStatus()->m_SubmittedFrames == 1,
			"Released payload capacity resumes the same frame and reference sample with one capture");
		GGLAB_UNUSED(payloadLimited.m_Sequence.Cancel());

		SequenceHarness impossible(directory.GetPath());
		GGLAB_UNUSED(impossible.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
			.m_CaptureFrames = { 0 } }, error));
		impossible.Frame();
		impossible.m_Control.m_Availability = FrameCaptureAvailability::TooLarge;
		impossible.Frame();
		context.Check(!impossible.m_Deferred && impossible.m_Control.m_Issued.empty() &&
			impossible.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed,
			"An oversized target fails its sequence instead of waiting forever for impossible capacity");
	}

	void RunSequenceJobOwnershipTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("sequence-job-ownership");
		{
			SequenceHarness harness(directory.GetPath(), { .m_MaxActiveJobs = 1, .m_MaxReceipts = 1 });
			harness.m_Paths.front().m_Keys.back().m_Frame = 179;
			std::vector<uint32_t> frames(180);
			std::iota(frames.begin(), frames.end(), 0u);
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test", .m_CaptureFrames = frames }, error));
			for (uint32_t tick = 0; tick < 190 && harness.m_Sequence.IsActive(); ++tick)
			{
				GGLAB_UNUSED(harness.Frame());
			}
			const auto& status = *harness.m_Sequence.GetStatus();
			const auto intake = harness.m_Capture.GetIntakeSnapshot();
			context.Check(status.m_State == FrameSequenceState::Completed && status.m_CompletedCaptures == 180 &&
				status.m_AcceptedCaptureJobs == 180 && status.m_FinishedCaptureJobs == 180 && status.m_PendingCaptureJobs == 0 &&
				intake.m_ActiveJobs == 0 && intake.m_ReservedReceipts == 0,
				"A 180-job sequence reuses one receipt slot without retaining completed job history");
			context.Check(harness.m_JobResults.size() == 180 &&
				std::ranges::all_of(harness.m_JobResults, [](const auto& result)
				{
					return result.m_Publication == CaptureJobPublication::Published &&
						std::filesystem::exists(result.m_ManifestPath);
				}), "Every sequence job finishes behind a real bundle manifest before its receipt is released");
		}
		for (uint32_t samples : { 0u, 3u })
		{
			SequenceHarness harness(directory.GetPath(), { .m_MaxActiveJobs = 1, .m_MaxReceipts = 1 });
			GGLAB_UNUSED(harness.Frame());
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test", .m_CaptureFrames = { 0 },
				.m_ReferenceSamples = samples }, error));
			uint64_t blocker = 0;
			harness.m_BeforeBeginFrame = [&]()
				{
					if (blocker == 0 && (!harness.m_LastSample || harness.m_LastSample->m_Index + 1 == samples))
					{
						blocker = harness.m_Capture.TrySubmit({ .m_RequiredProducts = { ImageCaptureProduct{} },
							.m_Timing = FrameCaptureTiming::AfterReady, .m_RequiredContentId = "Missing.Content" }).m_JobId;
					}
				};
			for (uint32_t attempt = 0; attempt < 4 && !harness.m_Deferred; ++attempt)
			{
				GGLAB_UNUSED(harness.Frame());
			}
			harness.m_BeforeBeginFrame = {};
			const auto time = harness.m_SimulationTime;
			const auto serial = harness.m_RenderSerial;
			context.Check(blocker != 0 && harness.m_Deferred && harness.m_Sequence.ShouldHoldTime() &&
				harness.m_Sequence.GetStatus()->m_AcceptedCaptureJobs == 0 && harness.m_Control.m_Issued.empty(),
				"Late Busy admission aborts without accepting a job, issuing work or advancing a sequence sample");
			GGLAB_UNUSED(harness.m_Capture.CancelJob(blocker));
			GGLAB_UNUSED(harness.Frame());
			context.Check(harness.m_Deferred && harness.m_RenderSerial == serial && harness.m_SimulationTime == time &&
				harness.m_Capture.GetIntakeSnapshot().m_ActiveJobs == 0,
				"An unconsumed terminal receipt alone blocks admission before simulation or posing");
			GGLAB_UNUSED(harness.m_Capture.TakeJobResult(blocker));
			GGLAB_UNUSED(harness.Frame());
			context.Check(!harness.m_Deferred && harness.m_SimulationTime == time && harness.m_JobResults.size() == 1 &&
				harness.m_JobResults.front().m_JobId == blocker + 1 &&
				harness.m_Results.front().m_Metadata->m_TotalTime == time &&
				harness.m_Sequence.GetStatus()->m_SubmittedFrames == 1,
				"Receipt release resumes the exact frame/sample and Busy admission consumes no job identity");
			GGLAB_UNUSED(harness.m_Sequence.Cancel());
		}
		for (bool published : { false, true })
		{
			SequenceHarness harness(directory.GetPath(), { .m_MaxActiveJobs = 1, .m_MaxReceipts = 1 });
			harness.m_AutoComplete = published;
			harness.m_CollectJobs = false;
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test", .m_CaptureFrames = { 0 } }, error));
			GGLAB_UNUSED(harness.Frame());
			GGLAB_UNUSED(harness.Frame());
			context.Check(harness.m_Sequence.Cancel() == FrameSequenceCancelVerdict::Cancelled && harness.m_Sequence.GetStatus()->m_PendingCaptureJobs == 1 &&
				harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test" }, error) == 0,
				"Cancellation keeps pending job ownership and prevents a new sequence from replacing its drain state");
			harness.m_CollectJobs = true;
			harness.CollectCaptureJobs();
			const auto intake = harness.m_Capture.GetIntakeSnapshot();
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Cancelled &&
				harness.m_Sequence.GetStatus()->m_PendingCaptureJobs == 0 && intake.m_ReservedReceipts == 0 &&
				harness.m_Sequence.GetStatus()->m_CompletedCaptures == (published ? 1u : 0u) &&
				harness.m_JobResults.size() == 1 &&
				harness.m_JobResults.front().m_Publication == (published ?
					CaptureJobPublication::Published : CaptureJobPublication::NotPublished) &&
				intake.m_DrainingRuntimeRequests == (published ? 0u : 1u),
				"A cancelled sequence consumes the real job receipt without rewriting publication or releasing pending Runtime work");
			if (!published)
			{
				harness.m_Control.Complete(harness.m_Control.m_Issued.front().m_Id);
				harness.m_Capture.Update();
				harness.CollectCaptureJobs();
			}
			context.Check(harness.m_Capture.GetIntakeSnapshot().m_DrainingRuntimeRequests == 0 &&
				harness.m_JobResults.size() == 1 && harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Cancelled,
				"Late completion drains cancelled owners once and cannot revive the sequence or emit another receipt");
		}
		{
			SequenceHarness harness(directory.GetPath(), { .m_MaxActiveJobs = 1, .m_MaxReceipts = 1,
				.m_MaxProductsPerJob = 1 });
			GGLAB_UNUSED(harness.Frame());
			const auto blocker = harness.m_Capture.TrySubmit({ .m_RequiredProducts = { ImageCaptureProduct{} },
				.m_Timing = FrameCaptureTiming::AfterReady, .m_RequiredContentId = "Missing.Content" });
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test", .m_CaptureFrames = { 0 },
				.m_Exports = { { .m_Frame = 0, .m_Targets = { { .m_Resource = "A" } } } } }, error));
			GGLAB_UNUSED(harness.Frame());
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
				harness.m_Sequence.GetStatus()->m_AcceptedCaptureJobs == 0 &&
				harness.m_Capture.GetIntakeSnapshot().m_ReservedReceipts == 1 &&
				harness.m_Capture.GetJobSnapshot(blocker.m_JobId).has_value(),
				"Invalid sequence plans are rejected before pressure checks without consuming an identity or foreign receipt");
			GGLAB_UNUSED(harness.m_Capture.CancelJob(blocker.m_JobId));
			GGLAB_UNUSED(harness.m_Capture.TakeJobResult(blocker.m_JobId));
		}
	}

	void RunReferenceSequenceTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-sequence-reference");
		SequenceHarness harness(directory.GetPath());
		std::string error;
		const uint64_t tooMany = harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
			.m_ReferenceSamples = MaxTemporalReferenceSamples + 1 }, error);
		std::string biasError;
		const uint64_t biasWithoutReference = harness.m_Sequence.Start({
			.m_CameraPathId = "SEQ_Test",
			.m_ReferenceTextureLodBias = -1.0f,
			}, biasError);
		const uint64_t id = harness.m_Sequence.Start({
			.m_CameraPathId = "SEQ_Test",
			.m_CaptureFrames = { 1 },
			.m_ReferenceSamples = 3,
			.m_ReferenceTextureLodBias = -0.75f,
			}, error);
		harness.Frame();

		std::vector<uint32_t> frames;
		std::vector<uint32_t> samples;
		std::vector<bool> heldBefore;
		std::vector<size_t> issued;
		bool everySampleBiased = true;
		while (harness.m_Sequence.IsActive() && frames.size() < 30)
		{
			heldBefore.push_back(harness.m_Sequence.ShouldHoldTime());
			const std::optional<uint32_t> frame = harness.Frame();
			frames.push_back(frame.value_or(999));
			samples.push_back(harness.m_LastSample ? harness.m_LastSample->m_Index : 999);
			everySampleBiased &= !harness.m_LastSample ||
				harness.m_LastSample->m_TextureLodBias == -0.75f;
			issued.push_back(harness.m_Control.m_Issued.size());
		}
		const FrameSequenceStatus& status = *harness.m_Sequence.GetStatus();
		context.Check(tooMany == 0 && id != 0 && frames.size() == 18 &&
			frames[0] == 0 && frames[2] == 0 && frames[3] == 1 && frames[17] == 5 &&
			samples[0] == 0 && samples[1] == 1 && samples[2] == 2 && samples[3] == 0 &&
			!heldBefore[0] && heldBefore[1] && heldBefore[2] && !heldBefore[3] &&
			status.m_State == FrameSequenceState::Completed &&
			status.m_SubmittedFrames == 6 && status.m_ReferenceSamples == 3,
			"A reference sequence renders every frame as consecutive samples with time held "
			"after the first");
		const auto capture = std::ranges::find_if(harness.m_Results,
			[](const CaptureProductResult& result)
			{
				return result.m_Metadata && result.m_Metadata->m_Sequence;
			});
		context.Check(issued[4] == 0 && issued[5] == 1 && harness.m_Results.size() == 1 &&
			capture != harness.m_Results.end() &&
			capture->m_Metadata->m_Sequence->m_Frame == 1 &&
			capture->m_Metadata->m_Sequence->m_ReferenceSamples == 3,
			"A reference frame is captured once, after its last sample");
		context.Check(biasWithoutReference == 0 && !biasError.empty() && everySampleBiased &&
			capture != harness.m_Results.end() &&
			capture->m_Metadata->m_Sequence->m_ReferenceTextureLodBias == -0.75f,
			"Every reference sample carries the requested texture LOD bias, recorded in the "
			"sidecar; it requires reference samples");
	}

	void RunFrameSequenceEvaluationTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-sequence-evaluation");
		{
			const std::vector<std::vector<LabParameterValue>> rejected
			{
				{ { LabParameterId("gtao.radius"), 1.0f } },
				{ { LabParameterId("view.taa.enabled"), 1u } },
				{ { LabParameterId("view.gtao.temporal_max_samples"), 2.5f } },
				{ { LabParameterId("view.gtao.temporal_max_samples"), -1 } },
				{ { LabParameterId("view.gtao.temporal_max_samples"), 0u } },
				{ { LabParameterId("view.taa.history_filter"), 7u } },
				{ { LabParameterId("view.gtao.radius"), std::numeric_limits<float>::quiet_NaN() } },
				{ { LabParameterId("view.taa.enabled"), true },
					{ LabParameterId("view.taa.enabled"), false } },
			};
			SequenceHarness harness(directory.GetPath());
			std::string error;
			for (const auto& values : rejected)
			{
				context.Check(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
					.m_Parameters = values }, error) == 0 && !error.empty() && !harness.m_Sequence.IsActive(),
					"Invalid sequence assignments reject atomically against the shared Host descriptors");
			}
			context.Check(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
				.m_Parameters = std::vector<LabParameterValue>(FrameSequenceMaxParameters + 1,
					{ LabParameterId("view.taa.enabled"), true }) }, error) == 0 && !error.empty(),
				"Sequence assignments have a bounded admission budget");
		}
		{
			SequenceHarness harness(directory.GetPath());
			FrameSequenceRequest request{
				.m_CameraPathId = "SEQ_Test",
				.m_Parameters = {
					{ LabParameterId("view.taa.enabled"), true },
					{ LabParameterId("view.gtao.radius"), 2u },
					{ LabParameterId("view.gtao.temporal_max_samples"), 8 },
					{ LabParameterId("view.taa.history_filter"), 0u } },
			};
			std::string error;
			const auto id = harness.m_Sequence.Start(request, error);
			request.m_Parameters.front().m_Value = false;
			harness.Frame();
			harness.Frame();
			context.Check(id != 0 && harness.m_LastViewPatch &&
				harness.m_LastViewPatch->m_TemporalAA.m_Enabled == true &&
				harness.m_LastViewPatch->m_TemporalAA.m_HistoryFilter == TemporalAAHistoryFilter::Bilinear &&
				harness.m_LastViewPatch->m_Lighting.m_GTAO.m_Radius == 2.0f &&
				harness.m_LastViewPatch->m_Lighting.m_GTAO.m_TemporalMaxSamples == 8u,
				"Sequence owns a normalized assignment snapshot with the same integer resolution as events");
			const auto cancelled = harness.m_Sequence.Cancel();
			harness.Frame();
			context.Check(cancelled == FrameSequenceCancelVerdict::Cancelled && !harness.m_LastViewPatch,
				"Cancellation removes the frame-local sequence assignments");
		}
		{
			SequenceHarness harness(directory.GetPath());
			std::string error;
			const auto id = harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
				.m_Parameters = { { LabParameterId("view.taa.enabled"), true } } }, error);
			harness.Frame();
			harness.Frame();
			harness.m_Ready = false;
			harness.Frame();
			const bool failed = harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed;
			harness.Frame();
			context.Check(id != 0 && failed && !harness.m_LastViewPatch,
				"A readiness failure removes the frame-local sequence assignments on the next frame");
		}
		{
			GTAOSettings content;
			content.m_Power = 2.0f;
			content.m_DenoiseRadius = 4;
			const auto patched = ApplyGTAOSettingsPatch({
				.m_DirectionCount = 999, .m_StepCount = 0,
				.m_TemporalAccumulation = false, .m_TemporalMaxSamples = 0 }, content);
			context.Check(!patched.m_TemporalAccumulation && patched.m_TemporalMaxSamples == 1 &&
				patched.m_DirectionCount == GTAOMaxDirectionCount && patched.m_StepCount == 1 &&
				patched.m_Power == 2.0f && patched.m_DenoiseRadius == 4 &&
				patched.m_Radius == content.m_Radius && patched.m_Enabled == content.m_Enabled,
				"All four typed sequence GTAO fields retain shared bounds and inherited content");
		}
		{
			TemporalAASettings content{};
			content.m_Enabled = true;
			content.m_DepthAbsoluteThreshold = 0.25f;
			const TemporalAASettings overridden = ApplyTemporalAASettingsPatch({
				.m_Enabled = false,
				.m_MaxHistoryFeedback = 0.9f,
				.m_NeighborhoodClampExpansion = 4.0f,
				.m_HistoryRelaxation = 0.0f,
				.m_HistoryAccumulation = TemporalAAHistoryAccumulation::CompatibilityAge,
				.m_HistoryRectification = TemporalAAHistoryRectification::BoundedVarianceClip,
				.m_VarianceClipGamma = 9.0f,
				.m_HistoryFilter = TemporalAAHistoryFilter::Bilinear,
				.m_CurrentFilter = TemporalAACurrentFilter::Point,
				.m_MotionSelection = TemporalAAMotionSelection::Center,
				.m_PostTemporalView = TemporalAAPostTemporalView::Jittered,
				.m_ResolutionPreset = TemporalAAResolutionPreset::Quality,
				}, content);
			TemporalAASettings invalidFilter = content;
			invalidFilter.m_HistoryFilter = static_cast<TemporalAAHistoryFilter>(7);
			invalidFilter.m_CurrentFilter = static_cast<TemporalAACurrentFilter>(7);
			invalidFilter.m_MotionSelection = static_cast<TemporalAAMotionSelection>(7);
			invalidFilter.m_PostTemporalView = static_cast<TemporalAAPostTemporalView>(7);
			invalidFilter.m_ResolutionPreset = static_cast<TemporalAAResolutionPreset>(7);
			invalidFilter.m_HistoryRectification = static_cast<TemporalAAHistoryRectification>(7);
			invalidFilter.m_VarianceClipGamma = std::numeric_limits<float>::quiet_NaN();
			context.Check(!overridden.m_Enabled && overridden.m_MaxHistoryFeedback == 0.9f &&
				overridden.m_DepthAbsoluteThreshold == 0.25f &&
				overridden.m_NeighborhoodClampExpansion == TemporalAAMaxNeighborhoodClampExpansion &&
				overridden.m_HistoryFilter == TemporalAAHistoryFilter::Bilinear &&
				content.m_HistoryFilter == TemporalAAHistoryFilter::CatmullRomClamped &&
				ResolveTemporalAASettings(invalidFilter).m_HistoryFilter == TemporalAAHistoryFilter::CatmullRomClamped &&
				overridden.m_CurrentFilter == TemporalAACurrentFilter::Point &&
				content.m_CurrentFilter == TemporalAACurrentFilter::Gaussian &&
				ResolveTemporalAASettings(invalidFilter).m_CurrentFilter == TemporalAACurrentFilter::Gaussian &&
				overridden.m_MotionSelection == TemporalAAMotionSelection::Center &&
				content.m_MotionSelection == TemporalAAMotionSelection::ClosestDepth &&
				ResolveTemporalAASettings(invalidFilter).m_MotionSelection == TemporalAAMotionSelection::ClosestDepth &&
				overridden.m_PostTemporalView == TemporalAAPostTemporalView::Jittered &&
				content.m_PostTemporalView == TemporalAAPostTemporalView::Unjittered &&
				ResolveTemporalAASettings(invalidFilter).m_PostTemporalView == TemporalAAPostTemporalView::Unjittered &&
				overridden.m_ResolutionPreset == TemporalAAResolutionPreset::Quality &&
				content.m_ResolutionPreset == TemporalAAResolutionPreset::Native &&
				ResolveTemporalAASettings(invalidFilter).m_ResolutionPreset == TemporalAAResolutionPreset::Native &&
				overridden.m_HistoryAccumulation == TemporalAAHistoryAccumulation::CompatibilityAge &&
				overridden.m_HistoryRelaxation == 0.0f &&
				content.m_HistoryRelaxation == TemporalAADefaultHistoryRelaxation &&
				content.m_HistoryAccumulation == TemporalAAHistoryAccumulation::EffectiveSamples &&
				overridden.m_HistoryRectification == TemporalAAHistoryRectification::BoundedVarianceClip &&
				overridden.m_VarianceClipGamma == TemporalAAMaxVarianceClipGamma &&
				content.m_HistoryRectification == TemporalAAHistoryRectification::MinMaxClamp &&
				ResolveTemporalAASettings(invalidFilter).m_HistoryRectification == TemporalAAHistoryRectification::MinMaxClamp &&
				ResolveTemporalAASettings(invalidFilter).m_VarianceClipGamma == TemporalAADefaultVarianceClipGamma &&
				ApplyTemporalAASettingsPatch({}, content) == ResolveTemporalAASettings(content),
				"Sequence Temporal AA overrides replace only set fields and stay within the "
				"settings ranges");
		}
		{
			const std::array<double, 5> samples{ 4.0, 1.0, 3.0, 2.0, 5.0 };
			const FrameSequenceTimingSummary summary = SummarizeFrameSequenceTiming(samples);
			context.Check(summary.m_Count == 5 && summary.m_Mean == 3.0 &&
				summary.m_Median == 3.0 && summary.m_P90 == 5.0 && summary.m_Min == 1.0 &&
				summary.m_Max == 5.0 && SummarizeFrameSequenceTiming({}).m_Count == 0,
				"Timing summaries report nearest-rank percentiles");
		}
		{
			SequenceHarness harness(directory.GetPath());
			std::string referenceError;
			const uint64_t reference = harness.m_Sequence.Start({
				.m_CameraPathId = "SEQ_Test",
				.m_ReferenceSamples = 2,
				.m_Parameters = { { LabParameterId("view.taa.max_history_feedback"), 0.9f } },
				}, referenceError);
			std::string error;
			const uint64_t id = harness.m_Sequence.Start({
				.m_CameraPathId = "SEQ_Test",
				.m_Parameters = { { LabParameterId("view.taa.neighborhood_clamp_expansion"), 0.5f } },
				}, error);
			harness.Frame();
			bool everyFrameOverridden = true;
			while (harness.m_Sequence.IsActive())
			{
				everyFrameOverridden &= harness.Frame().has_value() &&
					harness.m_LastViewPatch &&
					harness.m_LastViewPatch->m_TemporalAA.m_NeighborhoodClampExpansion == 0.5f &&
					!harness.m_LastViewPatch->m_TemporalAA.m_MaxHistoryFeedback;
			}
			context.Check(reference == 0 && !referenceError.empty() && id != 0 &&
				everyFrameOverridden && !harness.m_Sequence.GetStatus()->m_GpuTiming,
				"Every sequence frame carries the requested Temporal AA overrides; a reference "
				"rejects them");
			harness.Frame();
			context.Check(!harness.m_LastViewPatch, "Completion removes the frame-local sequence assignments");
		}
		{
			SequenceHarness harness(directory.GetPath());
			std::string overrideError;
			const uint64_t referenceWithOverrides = harness.m_Sequence.Start({
				.m_CameraPathId = "SEQ_Test",
				.m_ReferenceSamples = 2,
				.m_Parameters = { { LabParameterId("view.gtao.temporal_accumulation"), true } },
				}, overrideError);
			std::string tapError;
			const uint64_t referenceWithOtherTap = harness.m_Sequence.Start({
				.m_CameraPathId = "SEQ_Test",
				.m_CaptureSource = FrameCaptureSource::Diagnostic,
				.m_DiagnosticTap = PostProcessDebugTap::TemporalHistoryWeight,
				.m_ReferenceSamples = 2,
				}, tapError);
			std::string error;
			const uint64_t visibilityReference = harness.m_Sequence.Start({
				.m_CameraPathId = "SEQ_Test",
				.m_CaptureSource = FrameCaptureSource::Diagnostic,
				.m_DiagnosticTap = PostProcessDebugTap::GTAOFinalAO,
				.m_ReferenceSamples = 2,
				}, error);
			for (uint32_t frame = 0; frame < 4 && !harness.m_LastSample; ++frame)
			{
				harness.Frame();
			}
			context.Check(referenceWithOverrides == 0 && !overrideError.empty() &&
				referenceWithOtherTap == 0 && !tapError.empty() && visibilityReference != 0 &&
				harness.m_LastSample &&
				harness.m_LastSample->m_Signal == TemporalReferenceSignal::AmbientOcclusion &&
				harness.m_LastViewPatch &&
				!harness.m_LastViewPatch->m_Lighting.m_GTAO.m_TemporalAccumulation &&
				!harness.m_LastViewPatch->m_Lighting.m_GTAO.m_TemporalMaxSamples,
				"A reference averages GTAO visibility for the gtao-final-ao tap, rejects other "
				"taps and takes no GTAO overrides");
		}
		{
			SequenceHarness harness(directory.GetPath());
			std::string error;
			const uint64_t id = harness.m_Sequence.Start({
				.m_CameraPathId = "SEQ_Test",
				.m_Parameters = { { LabParameterId("view.gtao.temporal_accumulation"), true },
					{ LabParameterId("view.gtao.temporal_max_samples"), 8u } },
				}, error);
			harness.Frame();
			bool everyFrameOverridden = true;
			while (harness.m_Sequence.IsActive())
			{
				everyFrameOverridden &= harness.Frame().has_value() &&
					harness.m_LastViewPatch &&
					harness.m_LastViewPatch->m_Lighting.m_GTAO.m_TemporalAccumulation == true &&
					harness.m_LastViewPatch->m_Lighting.m_GTAO.m_TemporalMaxSamples == 8u;
			}
			const GTAOSettings overridden = ApplyGTAOSettingsPatch(
				{ .m_TemporalAccumulation = true, .m_TemporalMaxSamples = 999u }, GTAOSettings{});
			context.Check(id != 0 && everyFrameOverridden && overridden.m_TemporalAccumulation &&
				overridden.m_TemporalMaxSamples == GTAOMaxTemporalSamples &&
				ApplyGTAOSettingsPatch({}, GTAOSettings{}).m_TemporalMaxSamples == GTAOSettings{}.m_TemporalMaxSamples,
				"Every sequence frame carries the requested GTAO overrides, resolved to the "
				"settings' ranges");
		}
	}

	void RunFrameSequenceContinuityTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-sequence-continuity");
		for (uint32_t change = 0; change < 3; ++change)
		{
			for (const bool atCut : { false, true })
			{
				SequenceHarness harness(directory.GetPath());
				std::string error;
				GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test" }, error));
				harness.Frame(); // Readiness frame before sequence frame zero.
				const uint32_t framesBeforeChange = atCut ? 4u : 1u;
				for (uint32_t frame = 0; frame < framesBeforeChange; ++frame)
				{
					harness.Frame();
				}
				if (change == 0)
				{
					harness.m_RenderExtent = { 43, 22 };
				}
				if (change == 1)
				{
					harness.m_Accumulation = TemporalAAHistoryAccumulation::CompatibilityAge;
				}
				if (change == 2)
				{
					harness.m_HistoryCompatible = false;
				}
				harness.Frame();
				const FrameSequenceStatus& status = *harness.m_Sequence.GetStatus();
				// A path cut permits history loss; a simultaneous effective-identity
				// change is still an unexpected change to the experiment.
				const bool permitted = atCut && change == 2;
				context.Check((status.m_State == FrameSequenceState::Failed) != permitted &&
					status.m_SubmittedFrames == framesBeforeChange + (permitted ? 1u : 0u),
					std::format("Sequence continuity change {} at cut {} obeys the Runtime "
						"identity and history contract", change, atCut));
			}
		}
	}

	void RunSequenceLateDeferralTests(SelfTestContext& context) noexcept
	{
		for (const uint32_t samples : { 0u, 3u })
		{
			TemporaryDirectory directory("sequence-late-deferral");
			SequenceHarness harness(directory.GetPath());
			GGLAB_UNUSED(harness.Frame());
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test", .m_CaptureFrames = { 0 },
				.m_Exports = { { .m_Frame = 0, .m_Targets = { { .m_Resource = "A" } }, .m_IncludeGraph = true } },
				.m_ReferenceSamples = samples }, error));
			harness.m_LateDeferrals = 2;
			for (uint32_t attempt = 0; attempt < 4 && !harness.m_Deferred; ++attempt)
			{
				GGLAB_UNUSED(harness.Frame());
			}
			const double time = harness.m_SimulationTime;
			const auto serial = harness.m_RenderSerial;
			const auto accepted = harness.m_Sequence.GetStatus()->m_AcceptedCaptureJobs;
			context.Check(harness.m_Deferred && accepted == 1 && harness.m_Results.empty() &&
				harness.m_Sequence.GetStatus()->m_SubmittedFrames == 0 && harness.m_Sequence.ShouldHoldTime() &&
				(samples == 0 || (harness.m_LastSample && harness.m_LastSample->m_Index == samples - 1)),
				"Late Busy preserves the exact sequence frame and reference sample without a terminal result");
			harness.m_Control.m_ResourceFrameAvailability = FrameCaptureAvailability::Busy;
			GGLAB_UNUSED(harness.Frame());
			GGLAB_UNUSED(harness.Frame());
			context.Check(harness.m_RenderSerial == serial && harness.m_SimulationTime == time,
				"Waiting for complete-plan capacity after a late deferral does not simulate another frame");
			harness.m_Control.m_ResourceFrameAvailability = FrameCaptureAvailability::Available;
			GGLAB_UNUSED(harness.Frame());
			context.Check(harness.m_Deferred && harness.m_SimulationTime == time &&
				harness.m_Sequence.GetStatus()->m_AcceptedCaptureJobs == accepted && harness.m_Results.empty() &&
				harness.m_Capture.GetJobSnapshot(1).has_value() &&
				harness.m_Sequence.GetStatus()->m_SubmittedFrames == 0,
				"A second late Busy reuses the accepted job and simulation instant without advancing sample or time");
			GGLAB_UNUSED(harness.Frame());
			context.Check(!harness.m_Deferred && harness.m_SimulationTime == time && harness.m_Results.size() == 3 &&
				harness.m_Sequence.GetStatus()->m_AcceptedCaptureJobs == accepted &&
				harness.m_Sequence.GetStatus()->m_SubmittedFrames == 1 &&
				std::ranges::all_of(harness.m_Results, [&](const auto& result)
				{
					return result.m_Status == CaptureJobStatus::Completed && result.m_Metadata &&
						result.m_Metadata->m_FrameSerial == harness.m_RenderSerial &&
						result.m_Metadata->m_TotalTime == time &&
						result.m_Metadata->m_Sequence && result.m_Metadata->m_Sequence->m_Frame == 0;
				}),
				"Only the successful retry advances the sequence, "
				"with fresh render provenance for the same simulation instant");
			GGLAB_UNUSED(harness.Frame());
			context.Check(harness.m_SimulationTime == time + 1.0 / 60.0 && harness.m_Results.size() == 3 &&
				harness.m_Capture.GetUnfinishedRequestCount() == 0 && harness.m_Control.GetUnfinishedRequestCount() == 0,
				"The next sequence frame advances time exactly once and leaves no orphan reservation or duplicate result");
			harness.m_Capture.FinalizeAfterRenderHost();
		}
	}

	void RunSequenceGroupAdmissionTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("sequence-group-admission");
		{
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control, .m_DefaultOutputDirectory = directory.GetPath(),
				.m_ImageEncoder = &EncodeTestPng, .m_WriteOnCallingThread = true });
			CaptureTestJobs coordinatorJobs(coordinator, context);
			for (uint32_t index = 0; index < 8; ++index)
			{
				GGLAB_UNUSED(coordinatorJobs.Admit({ .m_RequiredProducts = { GraphCaptureProduct{ false } }, .m_WaitForCapacity = true }));
			}
			const FrameCaptureSequenceInfo sequence{ .m_SequenceId = 77, .m_Frame = 4 };
			const auto ids = coordinatorJobs.Admit({
				.m_RequiredProducts = { ImageCaptureProduct{}, ResourceCaptureProduct{ { { .m_Resource = "A" } } },
					GraphCaptureProduct{} },
				.m_Sequence = sequence,
			});
			const auto image = ids;
			const auto resources = ids;
			auto state = MakeFrameState(true);
			state.m_FrameSerial = 100;
			coordinator.BeginFrame(state);
			context.Check(control.m_Issued.size() == 9 && control.m_Issued[0].m_Source == FrameCaptureSource::Scene &&
				control.m_Issued[1].m_Source == FrameCaptureSource::ResourceExport && control.m_Issued[0].m_GroupId != 0 &&
				control.m_Issued[0].m_GroupId == control.m_Issued[1].m_GroupId && control.m_Issued[1].m_IncludeGraph &&
				coordinatorJobs.TakeResults().empty(),
				"PNG and resource output claim both writer slots before eight earlier ordinary graphs can block the group");
			for (const auto& issued : control.m_Issued)
			{
				control.Complete(issued.m_Id);
			}
			coordinator.OnFrameSubmitted();
			coordinator.Update();
			const auto completed = coordinatorJobs.TakeResults();
			const auto png = std::ranges::find(completed, image, &CaptureJobResult::m_JobId);
			const auto arrays = std::ranges::find(completed, resources, &CaptureJobResult::m_JobId);
			context.Check(completed.size() == 8 && png->m_Products.size() == 3 && png != completed.end() && arrays != completed.end() &&
				png->m_Status == CaptureJobStatus::Completed && arrays->m_Status == CaptureJobStatus::Completed &&
				png->m_Products.front().m_Metadata && arrays->m_Products.front().m_Metadata && png->m_Products.front().m_Metadata->m_FrameSerial == 100 && arrays->m_Products.front().m_Metadata->m_FrameSerial == 100 &&
				png->m_Products.front().m_Metadata->m_Sequence && arrays->m_Products.front().m_Metadata->m_Sequence &&
				png->m_Products.front().m_Metadata->m_Sequence->m_Frame == 4 && arrays->m_Products.front().m_Metadata->m_Sequence->m_Frame == 4,
				"The sequence PNG and arrays publish on the same admitted frame instead of entering rollback/re-admission livelock");
			state.m_FrameSerial = 101;
			coordinator.BeginFrame(state);
			context.Check(control.m_Issued.size() == 10 && control.m_Issued.back().m_Source == FrameCaptureSource::RenderGraph &&
				control.m_Issued.back().m_GroupId == 0,
				"The ninth writer reservation stays bounded and the remaining flexible graph resumes after completion");
			if (control.m_Issued.size() == 10)
			{
				control.Complete(control.m_Issued.back().m_Id);
			}
			coordinator.Update();
			const auto remaining = coordinatorJobs.TakeResults();
			context.Check(remaining.size() == 1 && remaining[0].m_Products.front().m_Metadata && remaining[0].m_Products.front().m_Metadata->m_FrameSerial == 101 &&
				coordinator.GetUnfinishedRequestCount() == 0,
				"Every request in the eight-graph/PNG/export burst completes without exceeding writer capacity");
		}
		{
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control, .m_DefaultOutputDirectory = directory.GetPath(),
				.m_ImageEncoder = &EncodeTestPng, .m_WriteOnCallingThread = true });
			CaptureTestJobs coordinatorJobs(coordinator, context);
			const FrameCaptureSequenceInfo sequence{ .m_SequenceId = 79, .m_Frame = 5 };
			GGLAB_UNUSED(coordinatorJobs.Admit({
				.m_RequiredProducts = { ImageCaptureProduct{}, ResourceCaptureProduct{ { { .m_Resource = "A" } } } },
				.m_Sequence = sequence,
			}));
			auto state = MakeFrameState(true);
			coordinator.BeginFrame(state);
			const uint64_t groupId = control.m_Issued.empty() ? 0 : control.m_Issued[0].m_GroupId;
			for (const auto& issued : control.m_Issued)
			{
				control.Busy(issued.m_Id);
			}
			coordinator.Update();
			context.Check(coordinatorJobs.TakeResults().empty() && coordinator.GetUnfinishedRequestCount() == 2,
				"A late whole-frame retry returns both group outputs to Waiting without a published failure");
			++state.m_FrameSerial;
			coordinator.BeginFrame(state);
			context.Check(control.m_Issued.size() == 4 && groupId != 0 && control.m_Issued[2].m_GroupId == groupId &&
				control.m_Issued[3].m_GroupId == groupId && control.m_Issued[2].m_FrameSerial == state.m_FrameSerial &&
				control.m_Issued[3].m_FrameSerial == state.m_FrameSerial,
				"Whole-frame retries keep their group association and bind both outputs to fresh metadata together");
			coordinator.FinalizeAfterRenderHost();
		}
		for (bool requestPressure : {false, true})
		{
			SequenceHarness harness(directory.GetPath());
			CaptureTestJobs externalJobs(harness.m_Capture, context);
			harness.m_AutoComplete = false;
			GGLAB_UNUSED(harness.Frame());
			const uint32_t occupied = requestPressure ? 1u : 8u;
			if (requestPressure)
			{
				harness.m_Control.m_RequestLimit = 2;
			}
			for (uint32_t index = 0; index < occupied; ++index)
			{
				GGLAB_UNUSED(externalJobs.Admit({ .m_RequiredProducts = { GraphCaptureProduct{ false } } }));
			}
			GGLAB_UNUSED(harness.Frame());
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test", .m_CaptureFrames = { 0 },
				.m_Exports = { { .m_Frame = 0, .m_Targets = { { .m_Resource = "A" } } } } }, error));
			const auto serial = harness.m_RenderSerial;
			GGLAB_UNUSED(harness.Frame());
			context.Check(harness.m_Deferred && harness.m_RenderSerial == serial && harness.m_Control.m_Issued.size() == occupied,
				"One free writer or Runtime slot cannot begin a two-output sequence frame or advance simulation");
			harness.m_Control.Complete(harness.m_Control.m_Issued.front().m_Id);
			harness.m_Capture.Update();
			GGLAB_UNUSED(externalJobs.TakeResults());
			GGLAB_UNUSED(harness.Frame());
			const auto& issued = harness.m_Control.m_Issued;
			context.Check(!harness.m_Deferred && issued.size() == occupied + 2 &&
				issued[occupied].m_GroupId != 0 && issued[occupied].m_GroupId == issued[occupied + 1].m_GroupId &&
				harness.m_Sequence.GetStatus()->m_SubmittedFrames == 1,
				"Releasing sufficient group capacity resumes the same sequence frame and admits both outputs together");
			harness.m_Capture.FinalizeAfterRenderHost();
		}
		{
			SequenceHarness harness(directory.GetPath());
			harness.m_Control.m_RequestLimit = 1;
			GGLAB_UNUSED(harness.Frame());
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test", .m_CaptureFrames = { 0 },
				.m_Exports = { { .m_Frame = 0, .m_Targets = { { .m_Resource = "A" } } } } }, error));
			GGLAB_UNUSED(harness.Frame());
			const auto resource = std::ranges::find_if(harness.m_Results, [](const auto& result)
				{
					return result.m_ResourceFailure == RGObservationFailure::TooLarge;
				});
			context.Check(!harness.m_Deferred && harness.m_Control.m_Issued.empty() &&
				harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed && resource != harness.m_Results.end() &&
				harness.m_Capture.GetUnfinishedRequestCount() == 0,
				"A two-output group that can never fit the Runtime request limit fails explicitly instead of deferring forever");
		}
		{
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control });
			CaptureTestJobs coordinatorJobs(coordinator, context);
			const FrameCaptureSequenceInfo sequence{ .m_SequenceId = 88 };
			const auto ids = coordinatorJobs.Admit({
				.m_RequiredProducts = { ImageCaptureProduct{}, ResourceCaptureProduct{ { { .m_Resource = "A" } } } },
				.m_Sequence = sequence,
			});
			const auto image = ids;
			control.m_ResourceFrameAvailability = FrameCaptureAvailability::Busy;
			coordinator.BeginFrame(MakeFrameState(true));
			context.Check(control.m_Issued.empty() && (coordinator.CancelJob(image) == CaptureCancelResult::Cancelled),
				"A busy sequence group remains wholly unissued and can be cancelled");
			const auto cancelled = coordinatorJobs.TakeResults();
			context.Check(cancelled.size() == 1 && cancelled[0].m_Products.size() == 2 && coordinator.GetUnfinishedRequestCount() == 0 &&
				std::ranges::all_of(cancelled, [](const auto& result) { return result.m_Status == CaptureJobStatus::Cancelled; }),
				"Cancelling one waiting sequence member cancels its whole admission group");
		}
	}

	void RunResourceSequenceTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("resource-sequence");
		SequenceHarness harness(directory.GetPath());
		std::string error;
		const auto id = harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test", .m_CaptureFrames = { 1 },
			.m_Exports = { { .m_Frame = 1, .m_Targets = { { .m_Resource = "A" },{ .m_Resource = "B" } }, .m_IncludeGraph = true } } }, error);
		context.Check(id != 0, "A sequence accepts a resource group paired with a PNG on the same frame");
		for (uint32_t i = 0; i < 15 && harness.m_Sequence.GetStatus()->m_State != FrameSequenceState::Completed; ++i)
		{
			GGLAB_UNUSED(harness.Frame());
		}
		const auto image = std::ranges::find(harness.m_Results, CaptureProductKind::Image, &CaptureProductResult::m_Kind);
		const auto resources = std::ranges::find(harness.m_Results, CaptureProductKind::Resource, &CaptureProductResult::m_Kind);
		context.Check(image != harness.m_Results.end() && resources != harness.m_Results.end() && image->m_Metadata && resources->m_Metadata &&
			image->m_Metadata->m_FrameSerial == resources->m_Metadata->m_FrameSerial && resources->m_Metadata->m_Sequence->m_Frame == 1 &&
			harness.m_Control.m_Issued.back().m_Defer,
			"Scheduled resource and image outputs share the exact sequence frame and use pre-submit group deferral");
		harness.m_Capture.FinalizeAfterRenderHost();

		SequenceHarness delayed(directory.GetPath());
		GGLAB_UNUSED(delayed.Frame()); // Ready frame before sequence startup.
		GGLAB_UNUSED(delayed.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
			.m_Exports = { { .m_Frame = 0, .m_Targets = { { .m_Resource = "A" } } } } }, error));
		delayed.m_Control.m_ResourceFrameAvailability = FrameCaptureAvailability::Busy;
		const auto serial = delayed.m_RenderSerial;
		GGLAB_UNUSED(delayed.Frame());
		context.Check(delayed.m_Deferred && delayed.m_RenderSerial == serial && delayed.m_Control.m_Issued.empty(),
			"Export pressure defers sequence frame zero before posing, simulating or issuing any capture");
		delayed.m_Control.m_ResourceFrameAvailability = FrameCaptureAvailability::Available;
		const auto frame = delayed.Frame();
		context.Check(frame == 0u && delayed.m_Results.size() == 1 && delayed.m_Results[0].m_Metadata->m_Sequence->m_Frame == 0,
			"The deferred sequence resumes its exact frame once the complete payload pool is available");
		delayed.m_Capture.FinalizeAfterRenderHost();
	}

	void RunFrameSequenceFailureTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-sequence-failure");
		for (const auto source : { FrameCaptureSource::RenderGraph,
			FrameCaptureSource::ResourceExport, static_cast<FrameCaptureSource>(255) })
		{
			SequenceHarness harness(directory.GetPath());
			std::string error;
			context.Check(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
				.m_CaptureSource = source }, error) == 0 && !error.empty() &&
				harness.m_Capture.GetIntakeSnapshot().m_ReservedReceipts == 0,
				"Sequence image selection rejects unsupported operations before creating capture ownership");
		}
		{
			SequenceHarness harness(directory.GetPath());
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Missing" }, error));
			harness.Frame();
			harness.Frame();
			const FrameSequenceStatus& status = *harness.m_Sequence.GetStatus();
			context.Check(status.m_State == FrameSequenceState::Failed &&
				status.m_Failure.find("SEQ_Test") != std::string::npos,
				"An unknown path fails and lists the available paths");
		}
		{
			SequenceHarness harness(directory.GetPath());
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start(
				{ .m_CameraPathId = "SEQ_Test", .m_CaptureFrames = { 6 } }, error));
			harness.Frame();
			harness.Frame();
			context.Check(
				harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed,
				"A capture frame beyond the path fails before frame 0");
		}
		{
			SequenceHarness harness(directory.GetPath());
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test" }, error));
			harness.Frame();
			harness.Frame();
			harness.Frame();
			harness.Frame(true, 2);
			const FrameSequenceStatus& status = *harness.m_Sequence.GetStatus();
			context.Check(status.m_State == FrameSequenceState::Failed &&
				status.m_SubmittedFrames == 2 && !harness.Frame(),
				"A temporal continuity change outside a path cut fails the sequence");
		}
		{
			SequenceHarness harness(directory.GetPath());
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test" }, error));
			harness.Frame();
			harness.Frame();
			harness.m_Ready = false;
			harness.Frame();
			context.Check(
				harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed,
				"A readiness gate leaving Ready fails a running sequence");
		}
		{
			SequenceHarness harness(directory.GetPath());
			std::string error;
			const uint64_t id = harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test" }, error);
			harness.Frame();
			harness.Frame();
			const bool cancelled = harness.m_Sequence.Cancel() == FrameSequenceCancelVerdict::Cancelled;
			context.Check(id != 0 && cancelled && !harness.Frame() &&
				harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Cancelled &&
				harness.m_Sequence.Cancel() == FrameSequenceCancelVerdict::Finished,
				"Cancelling stops posing frames; only an active sequence can be cancelled");
		}
	}
}

namespace gglab
{
	using namespace capture_test;

	void RunCaptureSequenceSelfTests(SelfTestContext& context) noexcept
	{
		RunResourceSequenceTests(context);
		RunSequenceGroupAdmissionTests(context);
		RunSequenceLateDeferralTests(context);
		RunFrameSequenceTests(context);
		RunFrameSequenceBackPressureTests(context);
		RunSequenceJobOwnershipTests(context);
		RunReferenceSequenceTests(context);
		RunFrameSequenceEvaluationTests(context);
		RunFrameSequenceContinuityTests(context);
		RunFrameSequenceFailureTests(context);
	}
}
