#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "Capture/CapturePublisher.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "Capture/NonFiniteWatchCoordinator.h"
#include "CaptureCoordinatorTestCases.h"
#include "CapturePublicationTestFileSystem.h"
#include "CaptureTestServices.h"
#include "FrameCaptureCoordinatorSelfTests.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchTypes.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfileFrameSnapshot.h"
#include "GGLabTestCore/SelfTest.h"
#include "SequenceTestServices.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gglab::capture_test
{
	GpuProfileFrameSnapshot MakeSequenceGpuProfile(uint64_t renderSerial) noexcept
	{
		return { .m_FrameIndex = renderSerial + 1000, .m_RenderFrameSerial = renderSerial,
			.m_FrameMilliseconds = static_cast<double>(renderSerial),
			.m_Samples = { { "PostProcess.TemporalAA", 0.5, 1 } } };
	}

	std::vector<uint64_t> SubmitShortTimingSequence(SequenceHarness& harness,
		FrameSequenceRequest request = {})
	{
		harness.m_Paths.front().m_Keys.resize(2);
		harness.m_Paths.front().m_Keys.back().m_Frame = 1;
		harness.m_Paths.front().m_Keys.back().m_Cut = true;
		request.m_CameraPathId = "SEQ_Test";
		request.m_GpuTiming = true;
		const uint32_t samples = 2 * std::max(1u, request.m_ReferenceSamples);
		std::string error;
		GGLAB_UNUSED(harness.m_Sequence.Start(std::move(request), error));
		harness.Frame();
		std::vector<uint64_t> serials;
		for (uint32_t index = 0; index < samples; ++index)
		{
			harness.Frame();
			serials.push_back(harness.m_RenderSerial);
		}
		return serials;
	}

	void DeliverTimingProfiles(SequenceHarness& harness, std::span<const uint64_t> serials)
	{
		for (const uint64_t serial : serials)
		{
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(serial), true);
		}
	}

	void RunSequenceTimingGraphObservationTests(SelfTestContext& context) noexcept
	{
		for (uint32_t variant = 0; variant < 3; ++variant)
		{
			TemporaryDirectory directory("timing-graph-observation");
			SequenceHarness harness(directory.GetPath());
			CaptureTestJobs graphJobs(harness.m_Capture, context);
			harness.m_AutoComplete = false;
			harness.m_Paths.front().m_Keys.resize(2);
			harness.m_Paths.front().m_Keys.back().m_Frame = 1;
			GGLAB_UNUSED(harness.Frame());
			CapturePlan plan{ .m_RequiredProducts = { GraphCaptureProduct{ variant == 1 } } };
			if (variant == 2)
			{
				plan.m_RequiredProducts.insert(plan.m_RequiredProducts.begin(),
					ResourceCaptureProduct{ { { .m_Resource = "A" } } });
			}
			const uint64_t graphJob = graphJobs.Admit(std::move(plan));
			std::string error;
			const uint64_t sequence = harness.m_Sequence.Start(
				{ .m_CameraPathId = "SEQ_Test", .m_GpuTiming = true }, error);
			const auto firstFrame = harness.Frame();
			const uint64_t firstSerial = harness.m_RenderSerial;
			const auto secondFrame = harness.Frame();
			const uint64_t secondSerial = harness.m_RenderSerial;
			context.Check(sequence != 0 && firstFrame == 0u && secondFrame == 1u &&
				harness.m_Control.m_Issued.size() == 1 &&
				harness.m_Control.m_Issued.front().m_FrameSerial == firstSerial &&
				harness.m_Control.GetUnfinishedRequestCount() == 1 && graphJobs.TakeResults().empty(),
				"A graph delivery remains outstanding while the next frame submits without an export");
			if (harness.m_Control.m_Issued.size() != 1)
			{
				continue;
			}
			// Retire the graph only after the next submission, then deliver profiles
			// out of order. Conditions must stay bound to each submitted frame.
			harness.m_Control.Complete(harness.m_Control.m_Issued.front().m_Id);
			harness.m_Capture.Update();
			const auto results = graphJobs.TakeResults();
			context.Check(results.size() == 1 && results.front().m_JobId == graphJob &&
				results.front().m_Status == CaptureJobStatus::Completed,
				"Delayed full, summary and resource-paired graphs retain their normal completion");
			const std::array serials{ secondSerial, firstSerial };
			DeliverTimingProfiles(harness, serials);
			const auto& status = *harness.m_Sequence.GetStatus();
			context.Check(status.m_State == FrameSequenceState::Completed &&
				status.m_TimingPublication->m_Files.size() == 1,
				"Delayed graph retirement still permits complete sequence timing publication");
			if (status.m_TimingPublication->m_Files.size() != 1)
			{
				continue;
			}
			const auto product = nlohmann::json::parse(
				ReadText(status.m_TimingPublication->m_Files.front()));
			const auto& samples = product["coverage"]["samples"];
			context.Check(samples.size() == 2 && samples[0]["frameSerial"] == secondSerial &&
				samples[0]["conditions"]["graph"] == false,
				"An earlier unretired graph must not mark the next timing sample as a graph export");
			context.Check(samples.size() == 2 && samples[1]["frameSerial"] == firstSerial &&
				samples[1]["conditions"]["graph"] == true,
				"The exporting frame keeps its graph condition after delivery and late GPU timing");
		}
	}

	void RunSequenceParameterPublicationTests(SelfTestContext& context) noexcept
	{
		for (bool timed : { false, true })
		{
			TemporaryDirectory directory("parameter-frame-membership");
			SequenceHarness harness(directory.GetPath());
			harness.m_FreezeParameters = true;
			harness.m_Paths.front().m_Keys.resize(2);
			harness.m_Paths.front().m_Keys.back().m_Frame = 1;
			std::string error;
			const auto id = harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
				.m_GpuTiming = timed, .m_ParameterProvenance = true }, error);
			harness.Frame();
			harness.Frame(false);
			const auto aborted = harness.m_RenderSerial;
			const auto count = harness.m_Sequence.GetStatus()->m_ParameterFrameCount;
			harness.Frame();
			const auto first = harness.m_RenderSerial;
			harness.Frame();
			const auto last = harness.m_RenderSerial;
			if (timed)
			{
				const std::array serials{ last, first };
				DeliverTimingProfiles(harness, serials);
			}
			const auto& status = *harness.m_Sequence.GetStatus();
			context.Check(id != 0 && count == 0 && status.m_State == FrameSequenceState::Completed &&
				status.m_ParameterFrameCount == 2 && status.m_ParameterPublication->m_Files.size() == 1,
				"Every submitted sequence frame requires parameter publication; aborted retries add no sample");
			if (status.m_ParameterPublication->m_Files.empty())
			{
				continue;
			}
			const auto product = nlohmann::json::parse(ReadText(status.m_ParameterPublication->m_Files.front()));
			const auto manifest = nlohmann::json::parse(ReadText(status.m_ParameterPublication->m_Manifest));
			context.Check(product["frames"][0]["parameters"]["frameSerial"] == first &&
				product["frames"][1]["parameters"]["frameSerial"] == last && first > aborted &&
				manifest["products"].size() == (timed ? 2 : 1) &&
				(!timed || status.m_ParameterPublication->m_Manifest == status.m_TimingPublication->m_Manifest),
				"Parameters cover uncaptured frames and share one range publication with out-of-order timing results");
		}
		for (bool missing : { false, true })
		{
			TemporaryDirectory directory("parameter-frame-required");
			auto files = std::make_shared<CapturePublicationTestFileSystem>();
			files->m_FailWrite = missing ? 0 : 1;
			SequenceHarness harness(directory.GetPath(), {}, files);
			harness.m_FreezeParameters = !missing;
			harness.m_Paths.front().m_Keys.resize(2);
			harness.m_Paths.front().m_Keys.back().m_Frame = 1;
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
				.m_ParameterProvenance = true }, error));
			for (uint32_t frame = 0; frame < 4 && harness.m_Sequence.IsActive(); ++frame)
			{
				harness.Frame();
			}
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
				harness.m_Sequence.GetStatus()->m_ParameterPublication->m_Manifest.empty(),
				"Missing frame provenance or a failed required write cannot report a completed sequence");
		}
	}

	void RunSequenceTimingPublicationTests(SelfTestContext& context) noexcept
	{
		{
			TemporaryDirectory directory("timing-membership");
			SequenceHarness harness(directory.GetPath());
			const auto serials = SubmitShortTimingSequence(harness, { .m_ReferenceSamples = 2 });
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Finishing &&
				harness.m_Sequence.GetStatus()->m_TimingPublication->m_Manifest.empty(),
				"Submitted reference frames cannot publish before their GPU timing arrives");
			for (size_t index : { 3u, 1u, 0u, 2u })
			{
				auto profile = MakeSequenceGpuProfile(serials[index]);
				profile.m_Samples = { { "Repeated", 1.0, 1, GpuProfileScopeKind::RenderGraphPass, 2 } };
				if (index == 1)
				{
					profile.m_Samples.push_back({ "Repeated", 3.0, 1, GpuProfileScopeKind::RenderGraphPass, 4 });
					profile.m_Samples.push_back({ "Repeated", 2.0, 1 });
					profile.m_ProfilingStateChanged = true;
				}
				harness.m_Sequence.UpdateGpuTiming(profile, true);
			}
			const auto& status = *harness.m_Sequence.GetStatus();
			context.Check(status.m_State == FrameSequenceState::Completed &&
				status.m_TimingPublication->m_Outcome == CapturePublicationOutcome::Published &&
				status.m_TimingPublication->m_Files.size() == 1,
				"A complete timing product reaches Completed only with its publication receipt");
			if (status.m_TimingPublication->m_Files.empty())
			{
				return;
			}
			const auto product = nlohmann::json::parse(ReadText(status.m_TimingPublication->m_Files.front()));
			const auto manifest = nlohmann::json::parse(ReadText(status.m_TimingPublication->m_Manifest));
			context.Check(product["coverage"]["requestedSamples"] == 4 && product["coverage"]["recordedSamples"] == 4 &&
				product["schemaVersion"] == 2 && manifest["products"][0]["schemaVersion"] == 2 &&
				product["coverage"]["samples"][0]["cpuBuildRecordMilliseconds"] ==
					static_cast<double>(serials[3]) * 0.125 &&
				product["coverage"]["samples"][0]["sequenceFrame"] == 1 &&
				product["coverage"]["samples"][0]["referenceSample"] == 1 &&
				product["coverage"]["samples"][2]["firstFrame"] == true &&
				product["coverage"]["samples"][0]["cameraCut"] == true &&
				product["scopes"][1]["sampleIndices"] == nlohmann::json::array({ 1 }) &&
				product["scopes"][2]["kind"] == "other" && !product["stablePassSet"].get<bool>() &&
				product["measurement"]["profilingStateChanged"] == true,
				"Timing preserves cuts, first/reference samples, arrival membership, varying pass sets and measurement changes");
			context.Check(manifest["consistency"] == "sequence-range" && manifest["sequence"] == product["sequence"] &&
				!manifest.contains("submission") && !manifest.contains("jobId") &&
				manifest["bundleId"].get<std::string>().ends_with("-s1-timing") &&
				manifest["products"][0]["files"].size() == 1,
				"Sequence bundles use a disjoint identity and discover their one product without SameFrame provenance");
			context.Check(harness.m_Sequence.Cancel() == FrameSequenceCancelVerdict::Finished,
				"Cancellation after terminal publication returns Finished without rewriting its receipt");
		}
		for (bool failCapture : { false, true })
		{
			TemporaryDirectory directory("timing-required-job");
			auto files = std::make_shared<CapturePublicationTestFileSystem>();
			SequenceHarness harness(directory.GetPath(), {}, files);
			harness.m_AutoComplete = false;
			const auto serials = SubmitShortTimingSequence(harness, { .m_CaptureFrames = { 0 } });
			DeliverTimingProfiles(harness, serials);
			context.Check(files->GetWriteCount() == 0 && harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Finishing,
				"Complete GPU timing cannot publish while a required capture job remains pending");
			const uint64_t request = harness.m_Control.m_Issued.front().m_Id;
			if (failCapture)
			{
				harness.m_Control.Fail(request, "Injected required capture failure.");
			}
			else
			{
				harness.m_Control.Complete(request);
			}
			harness.m_Capture.Update();
			harness.CollectCaptureJobs();
			context.Check(harness.m_Sequence.GetStatus()->m_State == (failCapture ? FrameSequenceState::Failed : FrameSequenceState::Completed) &&
				harness.m_Sequence.GetStatus()->m_TimingPublication->m_Outcome == (failCapture ?
					CapturePublicationOutcome::NotPublished : CapturePublicationOutcome::Published),
				"Required job failure prevents timing publication; success permits the shared publisher");
		}
		for (bool failWatch : { false, true })
		{
			TemporaryDirectory directory("timing-watch");
			auto files = std::make_shared<CapturePublicationTestFileSystem>();
			SequenceHarness harness(directory.GetPath(), {}, files, true, true);
			harness.m_Control.m_Instrumentation = true;
			std::string error;
			context.Check(harness.m_Watch.Configure({ .m_Mode = NonFiniteWatchMode::Fail, .m_Boundary = false }, error),
				"Timing coverage fixture configures required production evidence");
			const auto serials = SubmitShortTimingSequence(harness);
			DeliverTimingProfiles(harness, serials);
			harness.m_Sequence.UpdateWatchCoverage();
			context.Check(files->GetWriteCount() == 0 && harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Finishing,
				"Timing cannot publish while submitted-range watch coverage remains pending");
			for (uint64_t serial : serials)
			{
				harness.m_Watch.OnProductionFrame({ .m_FrameSerial = serial,
					.m_Claim = { failWatch ? NonFiniteClaimStatus::Failed : NonFiniteClaimStatus::Passed,
						failWatch ? NonFiniteWatchFailure::NonFiniteReplaced : NonFiniteWatchFailure::None } });
			}
			harness.m_Sequence.UpdateWatchCoverage();
			context.Check(harness.m_Sequence.GetStatus()->m_State == (failWatch ? FrameSequenceState::Failed : FrameSequenceState::Completed) &&
				harness.m_Sequence.GetStatus()->m_TimingPublication->m_Outcome == (failWatch ?
					CapturePublicationOutcome::NotPublished : CapturePublicationOutcome::Published),
				"Required watch failure prevents publication and complete clean coverage permits it");
		}
		for (uint32_t scenario = 0; scenario < 5; ++scenario)
		{
			TemporaryDirectory directory("timing-publication-cancel");
			auto files = std::make_shared<CapturePublicationTestFileSystem>();
			files->m_Block = scenario == 0 ? CapturePublicationTestFileSystem::Block::TemporaryWrite :
				CapturePublicationTestFileSystem::Block::Publication;
			files->m_FailPublish = scenario == 2 ? 1 : scenario == 3 ? 2 : 0;
			files->m_FailWithdraw = scenario == 3;
			SequenceHarness harness(directory.GetPath(), {}, files, false, false, std::chrono::milliseconds(1));
			harness.m_FreezeParameters = true;
			const auto serials = SubmitShortTimingSequence(harness, { .m_ParameterProvenance = true });
			DeliverTimingProfiles(harness, serials);
			context.Check(files->WaitEntered() && harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Finishing &&
				harness.m_Sequence.GetStatus()->m_TimingPublication->m_Pending,
				"Eligibility hands immutable timing to the common writer while the sequence stays Finishing");
			const auto cancellation = harness.m_Sequence.Cancel();
			context.Check(cancellation == (scenario == 0 ? FrameSequenceCancelVerdict::Cancelled : FrameSequenceCancelVerdict::TooLate),
				"Pre-claim cancellation withdraws timing; a claimed publication is explicitly TooLate");
			if (scenario == 0)
			{
				std::string error;
				context.Check(harness.m_Capture.GetIntakeSnapshot().m_SequenceTimingWrites == 1 &&
					harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test" }, error) == 0,
					"Cancelled timing keeps the writer reservation and blocks replacement until draining finishes");
			}
			if (scenario == 4)
			{
				harness.m_Capture.PrepareForShutdown();
				harness.m_Capture.FinalizeAfterRenderHost();
				harness.m_Sequence.UpdateWatchCoverage();
				context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
					harness.m_Sequence.GetStatus()->m_TimingPublication->m_Outcome == CapturePublicationOutcome::Indeterminate,
					"A claimed shutdown timeout keeps the indeterminate receipt instead of reporting Cancelled");
			}
			files->Release();
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while ((harness.m_Sequence.IsActive() || harness.m_Capture.GetIntakeSnapshot().m_SequenceTimingWrites != 0) &&
				std::chrono::steady_clock::now() < deadline)
			{
				harness.m_Sequence.UpdateWatchCoverage();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			const auto& status = *harness.m_Sequence.GetStatus();
			const auto expected = scenario == 0 ? FrameSequenceState::Cancelled :
				scenario == 1 ? FrameSequenceState::Completed : FrameSequenceState::Failed;
			const auto publication = scenario == 1 ? CapturePublicationOutcome::Published :
				scenario >= 3 ? CapturePublicationOutcome::Indeterminate : CapturePublicationOutcome::NotPublished;
			context.Check(status.m_State == expected && status.m_TimingPublication->m_Outcome == publication &&
				status.m_ParameterPublication->m_Outcome == publication && !status.m_ParameterPublication->m_Pending &&
				harness.m_Capture.GetIntakeSnapshot().m_SequenceTimingWrites == 0,
				"Timing and parameters share cancellation, real publication outcomes and late shutdown receipts");
		}
		{
			TemporaryDirectory directory("timing-cancel-before-eligibility");
			SequenceHarness harness(directory.GetPath());
			const auto serials = SubmitShortTimingSequence(harness);
			context.Check(harness.m_Sequence.Cancel() == FrameSequenceCancelVerdict::Cancelled,
				"Cancellation before timing eligibility returns Cancelled");
			DeliverTimingProfiles(harness, serials);
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Cancelled &&
				harness.m_Sequence.GetStatus()->m_TimingPublication->m_Outcome == CapturePublicationOutcome::NotPublished,
				"Late profiles cannot publish a cancelled sequence as a complete measurement");
		}
	}

	void RunFrameSequenceGpuTimingTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-sequence-gpu-timing");
		{
			SequenceHarness harness(directory.GetPath());
			harness.m_Paths.front().m_Keys.resize(2);
			harness.m_Paths.front().m_Keys.back().m_Frame = 1;
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start(
				{ .m_CameraPathId = "SEQ_Test", .m_GpuTiming = true }, error));
			harness.Frame();
			for (uint32_t frame = 0; frame < 2; ++frame)
			{
				harness.Frame();
				auto profile = MakeSequenceGpuProfile(harness.m_RenderSerial);
				profile.m_Samples = {
					{ "Repeated", 1.0, 1, GpuProfileScopeKind::RenderGraphPass, 2 },
					{ "Repeated", 3.0, 1, GpuProfileScopeKind::RenderGraphPass, 4 },
					{ "Repeated", 5.0, 2 },
				};
				if (frame == 1)
				{
					std::reverse(profile.m_Samples.begin(), profile.m_Samples.end());
				}
				harness.m_Sequence.UpdateGpuTiming(profile, true);
			}
			const auto& status = *harness.m_Sequence.GetStatus();
			const auto& scopes = status.m_GpuTiming->m_Scopes;
			context.Check(status.m_State == FrameSequenceState::Completed && scopes.size() == 3 &&
				scopes[0].m_Kind == GpuProfileScopeKind::RenderGraphPass && scopes[0].m_Occurrence == 2 &&
				scopes[0].m_Milliseconds == std::vector<double>{ 1.0, 1.0 } &&
				scopes[1].m_Kind == GpuProfileScopeKind::RenderGraphPass && scopes[1].m_Occurrence == 4 &&
				scopes[1].m_Milliseconds == std::vector<double>{ 3.0, 3.0 } &&
				scopes[2].m_Kind == GpuProfileScopeKind::Other && scopes[2].m_Occurrence == 0 &&
				scopes[2].m_Milliseconds == std::vector<double>{ 5.0, 5.0 },
				"Sequence series retain same-name pass and other identities when sample order changes");
		}
		for (uint32_t delay : { 0u, 1u, 3u, 7u })
		{
			SequenceHarness harness(directory.GetPath());
			harness.m_Paths.front().m_Keys.back().m_Frame = 8;
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start(
				{ .m_CameraPathId = "SEQ_Test", .m_GpuTiming = true }, error));
			harness.Frame(); // A ready ordinary frame before sequence frame zero.
			const uint64_t firstSerial = harness.m_RenderSerial + 1;
			bool sawFinishing = false;
			for (uint32_t step = 0; harness.m_Sequence.IsActive() && step < 32; ++step)
			{
				harness.Frame();
				sawFinishing |= harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Finishing;
				const auto profile = MakeSequenceGpuProfile(harness.m_RenderSerial > delay ? harness.m_RenderSerial - delay : 0);
				harness.m_Sequence.UpdateGpuTiming(profile, true);
				harness.m_Sequence.UpdateGpuTiming(profile, true);
			}
			const auto& status = *harness.m_Sequence.GetStatus();
			context.Check(status.m_State == FrameSequenceState::Completed && sawFinishing &&
				status.m_SubmittedFrames == 9 && status.m_GpuTiming->m_FrameMilliseconds.size() == 9 &&
				status.m_GpuTiming->m_FrameMilliseconds.front() == static_cast<double>(firstSerial) &&
				status.m_GpuTiming->m_FrameMilliseconds.back() == static_cast<double>(firstSerial + 8) &&
				status.m_GpuTiming->m_Samples.front().m_Observation.m_CpuBuildRecordMilliseconds ==
					static_cast<double>(firstSerial) * 0.125 &&
				status.m_GpuTiming->m_Samples.back().m_Observation.m_CpuBuildRecordMilliseconds ==
					static_cast<double>(firstSerial + 8) * 0.125 &&
				status.m_GpuTiming->m_Scopes.front().m_Milliseconds.size() == 9,
				std::format("Serial membership includes every sequence frame and its delayed tail (delay {})", delay));
		}
		{
			SequenceHarness harness(directory.GetPath());
			harness.m_Paths.front().m_Keys.resize(2);
			harness.m_Paths.front().m_Keys.back().m_Frame = 1;
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start(
				{ .m_CameraPathId = "SEQ_Test", .m_GpuTiming = true }, error));
			harness.Frame();
			harness.Frame();
			const uint64_t first = harness.m_RenderSerial;
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(first + 1), true);
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(first - 1), true);
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(0), true);
			context.Check(harness.m_Sequence.GetStatus()->m_GpuTiming->m_FrameMilliseconds.empty(),
				"Future, stale and unbound profiles are excluded even with valid profiler counters");
			harness.Frame(false);
			const uint64_t aborted = harness.m_RenderSerial;
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(aborted), true);
			context.Check(harness.m_Sequence.GetStatus()->m_GpuTiming->m_FrameMilliseconds.empty(),
				"A begun but unsubmitted render never becomes eligible for sequence GPU timing");
			harness.Frame();
			const uint64_t last = harness.m_RenderSerial;
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Finishing &&
				harness.m_Sequence.WantsGpuTiming(),
				"A short sequence keeps profiling enabled while its tail is pending");
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(last), true);
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(last), true);
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(first), true);
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Completed &&
				harness.m_Sequence.GetStatus()->m_GpuTiming->m_FrameMilliseconds.size() == 2,
				"Distinct bound profiles can complete out of publication order without counting duplicates");
			GGLAB_UNUSED(harness.m_Sequence.Start(
				{ .m_CameraPathId = "SEQ_Test", .m_GpuTiming = true }, error));
			harness.Frame();
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(last), true);
			context.Check(harness.m_Sequence.GetStatus()->m_GpuTiming->m_FrameMilliseconds.empty(),
				"A consecutive sequence rejects a retained profile from the preceding sequence");
			context.Check(harness.m_Sequence.Cancel() == FrameSequenceCancelVerdict::Cancelled && !harness.m_Sequence.WantsGpuTiming(),
				"Cancellation releases pending timing work and profiling ownership");
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(harness.m_RenderSerial), true);
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Cancelled &&
				harness.m_Sequence.GetStatus()->m_GpuTiming->m_FrameMilliseconds.empty(),
				"A cancelled sequence cannot be revived by a late GPU profile");
		}
		{
			SequenceHarness harness(directory.GetPath());
			harness.m_Paths.front().m_Keys.resize(2);
			harness.m_Paths.front().m_Keys.back().m_Frame = 1;
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
				.m_ReferenceSamples = 3, .m_GpuTiming = true }, error));
			harness.Frame();
			for (uint32_t sample = 0; sample < 6; ++sample)
			{
				harness.Frame();
				harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(harness.m_RenderSerial - 1), true);
			}
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Finishing &&
				harness.m_Sequence.GetStatus()->m_SubmittedFrames == 2 &&
				harness.m_Sequence.GetStatus()->m_GpuTiming->m_FrameMilliseconds.size() == 5,
				"Reference timing counts submitted samples independently of sequence frames");
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(harness.m_RenderSerial), true);
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Completed &&
				harness.m_Sequence.GetStatus()->m_GpuTiming->m_FrameMilliseconds.size() == 6,
				"The last reference sample is drained before sequence completion");
		}
		{
			SequenceHarness harness(directory.GetPath());
			harness.m_Paths.front().m_Keys.resize(2);
			harness.m_Paths.front().m_Keys.back().m_Frame = 1;
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
				.m_CaptureFrames = { 1 }, .m_GpuTiming = true }, error));
			harness.Frame();
			harness.Frame();
			const uint64_t first = harness.m_RenderSerial;
			harness.m_Control.m_Availability = FrameCaptureAvailability::Busy;
			for (uint32_t poll = 0; poll < 100; ++poll)
			{
				harness.Frame();
				harness.m_Sequence.UpdateGpuTiming({}, true);
			}
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(first), true);
			context.Check(harness.m_Deferred && harness.m_RenderSerial == first &&
				harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Running &&
				harness.m_Sequence.GetStatus()->m_GpuTiming->m_FrameMilliseconds.size() == 1,
				"Deferred frames can collect published profiles without consuming the submission budget");
			harness.m_Control.m_Availability = FrameCaptureAvailability::Available;
			harness.Frame();
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(harness.m_RenderSerial), true);
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Completed &&
				harness.m_Sequence.GetStatus()->m_GpuTiming->m_FrameMilliseconds.size() == 2,
				"Resuming a deferred capture preserves both sequence timing samples");
		}
		for (bool completeCaptureFirst : { false, true })
		{
			SequenceHarness harness(directory.GetPath());
			harness.m_Paths.front().m_Keys.resize(2);
			harness.m_Paths.front().m_Keys.back().m_Frame = 1;
			harness.m_AutoComplete = false;
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
				.m_CaptureFrames = { 1 }, .m_GpuTiming = true }, error));
			harness.Frame();
			harness.Frame();
			const uint64_t first = harness.m_RenderSerial;
			harness.Frame();
			const uint64_t last = harness.m_RenderSerial;
			const auto completeCapture = [&]()
			{
				harness.m_Control.Complete(harness.m_Control.m_Issued.front().m_Id);
				harness.m_Capture.Update();
				harness.CollectCaptureJobs();
			};
			if (completeCaptureFirst)
			{
				completeCapture();
			}
			else
			{
				harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(first), true);
				harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(last), true);
				context.Check(!harness.m_Sequence.WantsGpuTiming(),
					"Finished GPU collection releases profiling while capture writing is still pending");
			}
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Finishing,
				"Sequence completion waits for both capture writing and all bound GPU profiles");
			if (completeCaptureFirst)
			{
				harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(first), true);
				harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(last), true);
			}
			else
			{
				completeCapture();
			}
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Completed,
				"Capture/profile completion is independent of which owner finishes first");
		}
		{
			SequenceHarness harness(directory.GetPath());
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start(
				{ .m_CameraPathId = "SEQ_Test", .m_GpuTiming = true }, error));
			harness.m_Sequence.UpdateGpuTiming({}, false);
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
				harness.m_Sequence.GetStatus()->m_Failure.find("unavailable or disabled") != std::string::npos,
				"Unavailable profiling fails a timing request explicitly instead of completing with zero samples");
		}
		{
			SequenceHarness harness(directory.GetPath());
			harness.m_Paths.front().m_Keys.resize(2);
			harness.m_Paths.front().m_Keys.back().m_Frame = 1;
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start(
				{ .m_CameraPathId = "SEQ_Test", .m_GpuTiming = true }, error));
			harness.Frame();
			harness.Frame();
			const uint64_t missing = harness.m_RenderSerial;
			harness.Frame();
			harness.m_Sequence.UpdateGpuTiming(MakeSequenceGpuProfile(harness.m_RenderSerial), true);
			for (uint32_t poll = 0; poll < 100; ++poll)
			{
				harness.m_Sequence.UpdateGpuTiming({}, true);
			}
			for (uint32_t abort = 0; abort < 10; ++abort)
			{
				harness.Frame(false);
			}
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Finishing,
				"Polling and aborted work do not consume the successful-submission drain budget");
			for (uint32_t drain = 1; drain + 1 < FrameSequenceCoordinator::MaxPendingGpuTimingSubmissions; ++drain)
			{
				harness.Frame();
				harness.m_Sequence.UpdateGpuTiming({}, true);
			}
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Finishing,
				"The drain budget allows the last submission before its explicit failure boundary");
			harness.Frame();
			harness.m_Sequence.UpdateGpuTiming({}, true);
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
				harness.m_Sequence.GetStatus()->m_Failure.find(std::format("RenderFrame {}", missing)) != std::string::npos &&
				harness.m_Sequence.GetStatus()->m_Failure.find("sequence frame 0, reference sample 0") != std::string::npos &&
				harness.m_Sequence.GetStatus()->m_GpuTiming->m_FrameMilliseconds.size() == 1,
				"Missing tail profiles fail within a bounded number of successful submissions with their exact identity");
		}
		for (bool invalidScope : { false, true })
		{
			SequenceHarness harness(directory.GetPath());
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start(
				{ .m_CameraPathId = "SEQ_Test", .m_GpuTiming = true }, error));
			harness.Frame();
			harness.Frame();
			auto profile = MakeSequenceGpuProfile(harness.m_RenderSerial);
			if (invalidScope)
			{
				profile.m_Samples.front().m_Milliseconds = -1.0;
			}
			else
			{
				profile.m_FrameMilliseconds = std::numeric_limits<double>::quiet_NaN();
			}
			harness.m_Sequence.UpdateGpuTiming(profile, true);
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
				harness.m_Sequence.GetStatus()->m_GpuTiming->m_FrameMilliseconds.empty(),
				"Invalid frame/scope timings fail atomically without contaminating the summary");
		}
	}
}

namespace gglab
{
	using namespace capture_test;

	void RunCaptureTimingSelfTests(SelfTestContext& context) noexcept
	{
		RunFrameSequenceGpuTimingTests(context);
		RunSequenceTimingGraphObservationTests(context);
		RunSequenceTimingPublicationTests(context);
		RunSequenceParameterPublicationTests(context);
	}
}
