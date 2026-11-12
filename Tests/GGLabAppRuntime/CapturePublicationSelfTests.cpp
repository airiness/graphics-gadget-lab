#include "Capture/CaptureBundle.h"
#include "Capture/CaptureEnvironment.h"
#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "Capture/CapturePublisher.h"
#include "Capture/CaptureWriter.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureMetadata.h"
#include "CaptureCoordinatorTestCases.h"
#include "CapturePublicationTestFileSystem.h"
#include "CaptureTestServices.h"
#include "FrameCaptureCoordinatorSelfTests.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabTestCore/SelfTest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gglab::capture_test
{
	void RunNameCollisionTests(SelfTestContext& context) noexcept
	{
		for (const uint32_t collision : { 1u, 2u, 3u })
		{
			TemporaryDirectory directory("frame-capture-names");
			FakeCaptureControl control;
			auto files = std::make_shared<CapturePublicationTestFileSystem>();
			files->m_CollidePublish = collision;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
				.m_WriteOnCallingThread = true, .m_PublicationFileSystem = files });
			const auto admission = coordinator.TrySubmit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
			coordinator.BeginFrame(MakeFrameState(true));
			control.Complete(control.m_Issued.front().m_Id);
			coordinator.Update();
			const auto result = coordinator.TakeJobResult(admission.m_JobId);
			context.Check(result && result->m_Status == CaptureJobStatus::Completed &&
				result->m_Publication == CaptureJobPublication::Published &&
				result->m_ManifestPath.filename().string().ends_with("-2.capture-bundle.json") &&
				ReadText(files->m_CollisionPath) == "existing capture" && !HasPartialFile(directory.GetPath()),
				"Image, sidecar and manifest collisions never overwrite another publisher's file");
			if (result)
			{
				const auto image = ProductFile(*result, ".png");
				const auto sidecar = nlohmann::json::parse(ReadText(ProductFile(*result, ".json")));
				const auto manifest = nlohmann::json::parse(ReadText(result->m_ManifestPath));
				context.Check(sidecar["image"]["file"] == GetFrameCapturePathText(image.filename()) &&
					manifest["products"][0]["files"][0] == GetFrameCapturePathText(image.filename()) &&
					result->m_Products.front().m_Metadata->m_ImageFile == GetFrameCapturePathText(image.filename()),
					"Collision-resolved image, sidecar, manifest and receipt retain the same final paths");
			}
		}
	}

	void RunWriterThreadTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-writer");
		FakeCaptureControl control;
		const std::thread::id testThread = std::this_thread::get_id();
		auto encoderThread = std::make_shared<std::atomic<bool>>(false);
		FrameCaptureCoordinator coordinator({
			.m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(),
			// The platform encoder runs on a thread that never initialized COM.
			.m_ImageEncoder = [testThread, encoderThread](const FrameCaptureImage& image) noexcept
			{
				encoderThread->store(std::this_thread::get_id() != testThread);
				return EncodeTestPng(image);
			},
		});
		CaptureTestJobs coordinatorJobs(coordinator, context);
		const uint64_t id = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		coordinator.BeginFrame(MakeFrameState(true));
		control.Complete(control.m_Issued[0].m_Id);

		std::vector<CaptureJobResult> results;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (results.empty() && std::chrono::steady_clock::now() < deadline)
		{
			coordinator.Update();
			for (CaptureJobResult& ready : coordinatorJobs.TakeResults())
			{
				results.push_back(std::move(ready));
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		context.Check(results.size() == 1 && results[0].m_JobId == id &&
			results[0].m_Status == CaptureJobStatus::Completed &&
			HasPngSignature(ProductFile(results[0], ".png")) && encoderThread->load(),
			"The writer thread encodes off the frame thread and publishes asynchronously");
		coordinator.PrepareForShutdown();
		coordinator.FinalizeAfterRenderHost();
	}

	// Holds the encoder until released, standing in for file I/O that blocks.
	struct EncoderGate
	{
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		bool m_Released = false;
		bool m_Entered = false;
	};

	void RunCapacityAdmissionTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-admission");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({
			.m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(),
			.m_ImageEncoder = &EncodeTestPng,
			.m_WriteOnCallingThread = true,
		});
		CaptureTestJobs coordinatorJobs(coordinator, context);
		for (const auto availability : { FrameCaptureAvailability::Busy,
			FrameCaptureAvailability::TooLarge, FrameCaptureAvailability::Unsupported })
		{
			control.m_Availability = availability;
			const uint64_t id = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
			coordinator.BeginFrame(MakeFrameState(true));
			const auto failed = coordinatorJobs.TakeResults();
			context.Check(failed.size() == 1 && failed[0].m_JobId == id &&
				failed[0].m_Status == CaptureJobStatus::Failed &&
				!failed[0].m_Products.front().m_Failure.empty() && control.m_Issued.empty(),
				"Unavailable payload capacity fails an ordinary capture before GPU work");
		}
		control.m_Availability = FrameCaptureAvailability::Available;
		control.m_RejectRequest = true;
		const uint64_t rejected = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		coordinator.BeginFrame(MakeFrameState(true));
		const auto noSlot = coordinatorJobs.TakeResults();
		context.Check(noSlot.size() == 1 && noSlot[0].m_JobId == rejected &&
			noSlot[0].m_Status == CaptureJobStatus::Failed &&
			coordinator.GetUnfinishedRequestCount() == 0 && control.m_Issued.empty(),
			"A zero Runtime request id fails exactly once without reserving a write slot");
		control.m_RejectRequest = false;
		for (uint32_t index = 0; index < FrameCaptureCoordinator::MaxInFlightCaptures; ++index)
		{
			GGLAB_UNUSED(coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } }));
		}
		coordinator.BeginFrame(MakeFrameState(true));
		const uint64_t excess = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		coordinator.BeginFrame(MakeFrameState(true));
		const auto overflow = coordinatorJobs.TakeResults();
		context.Check(control.m_Issued.size() == FrameCaptureCoordinator::MaxInFlightCaptures &&
			overflow.size() == 1 && overflow[0].m_JobId == excess &&
			coordinator.GetCaptureAvailability({ .m_RequiredProducts = { ImageCaptureProduct{} } }) == FrameCaptureAvailability::Busy,
			"GPU captures still awaiting results reserve every write slot before any encoding begins");
		control.Fail(control.m_Issued.front().m_Id, "Submission failed.");
		coordinator.Update();
		GGLAB_UNUSED(coordinatorJobs.TakeResults());
		const uint64_t cancelled = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
		context.Check((coordinator.CancelJob(cancelled) == CaptureCancelResult::Cancelled) &&
			coordinator.GetCaptureAvailability({ .m_RequiredProducts = { ImageCaptureProduct{} } }) == FrameCaptureAvailability::Available,
			"Submission failure restores write capacity and cancelling a waiting request consumes no slot");
		GGLAB_UNUSED(coordinatorJobs.TakeResults());
		GGLAB_UNUSED(coordinatorJobs.Admit({
			.m_RequiredProducts = { DiagnosticCaptureProduct{ PostProcessDebugTap::TemporalHistoryWeight } },
		}));
		coordinator.BeginFrame(MakeFrameState(true));
		context.Check(control.m_QueriedFormat == RHIFormat::R8G8B8A8Unorm &&
			control.m_QueriedExtent == std::array<uint32_t, 2>{ 64, 32 } &&
				control.m_Issued.back().m_Source == FrameCaptureSource::Diagnostic,
			"Diagnostic admission queries the display-sized RGBA8 target independently of presentation format");
		coordinator.PrepareForShutdown();
		coordinator.FinalizeAfterRenderHost();
		const auto shutdown = coordinatorJobs.TakeResults();
		context.Check(shutdown.size() == FrameCaptureCoordinator::MaxInFlightCaptures &&
			coordinator.GetUnfinishedRequestCount() == 0 && !HasAnyFile(directory.GetPath()),
			"Shutdown finishes every reserved or cancelled request without allocating or writing an image");
	}

	void RunDeferredCapacityTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-deferred");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({ .m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(),
			.m_ImageEncoder = &EncodeTestPng, .m_WriteOnCallingThread = true });
		CaptureTestJobs coordinatorJobs(coordinator, context);
		const uint64_t viewA = coordinatorJobs.Admit({
			.m_RequiredProducts = { ImageCaptureProduct{} },
			.m_Timing = FrameCaptureTiming::AfterReady,
			.m_SettleFrames = 1,
			.m_ReferenceViewId = "CAM_A",
			.m_WaitForCapacity = true,
		});
		const uint64_t viewB = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} }, .m_ReferenceViewId = "CAM_B", .m_WaitForCapacity = true });
		control.m_Availability = FrameCaptureAvailability::Busy;
		coordinator.BeginFrame(MakeViewFrameState(0));
		coordinator.OnFrameSubmitted();
		context.Check(!coordinator.GetPendingViewChange() && control.m_Issued.empty() &&
			coordinatorJobs.TakeResults().empty() && coordinator.GetUnfinishedRequestCount() == 2,
			"A flexible view batch waits for Runtime capacity before changing its camera");

		control.m_Availability = FrameCaptureAvailability::Available;
		const auto change = coordinator.GetPendingViewChange();
		context.Check(change && change->m_JobId == viewA,
			"Capacity release resumes the first waiting view in submission order");
		coordinator.OnReferenceViewApplied(viewA, true);
		// Capacity can change between the camera query and the actual frame.
		control.m_Availability = FrameCaptureAvailability::Busy;
		coordinator.BeginFrame(MakeViewFrameState(1));
		coordinator.OnFrameSubmitted();
		coordinator.BeginFrame(MakeViewFrameState(1));
		coordinator.OnFrameSubmitted();
		context.Check(control.m_Issued.empty() && !coordinator.GetPendingViewChange() &&
			coordinatorJobs.TakeResults().empty() && coordinator.GetUnfinishedRequestCount() == 2,
			"Busy at issue keeps an applied view waiting and prevents the next camera from overtaking it");

		control.m_Availability = FrameCaptureAvailability::Available;
		auto state = MakeViewFrameState(1);
		state.m_FrameSerial = 42;
		state.m_FrameIndex = 42;
		coordinator.BeginFrame(state);
		coordinator.OnFrameSubmitted();
		context.Check(control.m_Issued.size() == 1 && control.m_Issued[0].m_FrameSerial == 42,
			"A deferred capture binds only the actual frame that admits it");
		if (!control.m_Issued.empty())
		{
			control.Complete(control.m_Issued[0].m_Id);
		}
		coordinator.Update();
		const auto completed = coordinatorJobs.TakeResults();
		context.Check(completed.size() == 1 && completed[0].m_JobId == viewA &&
			completed[0].m_Status == CaptureJobStatus::Completed && completed[0].m_Products.front().m_Metadata &&
			completed[0].m_Products.front().m_Metadata->m_FrameSerial == 42 &&
			completed[0].m_Products.front().m_Metadata->m_Camera.m_ReferenceViewId == "CAM_A",
			"Deferred view metadata records its admitted frame and camera exactly once");
		control.m_Availability = FrameCaptureAvailability::Busy;
		context.Check((coordinator.CancelJob(viewB) == CaptureCancelResult::Cancelled) && coordinator.GetUnfinishedRequestCount() == 0,
			"A view waiting for capacity remains cancellable without reserving a slot");
		GGLAB_UNUSED(coordinatorJobs.TakeResults());

		for (const auto availability : { FrameCaptureAvailability::TooLarge,
			FrameCaptureAvailability::Unsupported, FrameCaptureAvailability::Finalized })
		{
			control.m_Availability = availability;
			const uint64_t id = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} }, .m_WaitForCapacity = true });
			coordinator.BeginFrame(state);
			const auto failed = coordinatorJobs.TakeResults();
			context.Check(failed.size() == 1 && failed[0].m_JobId == id &&
				failed[0].m_Status == CaptureJobStatus::Failed && !failed[0].m_Products.front().m_Metadata &&
				coordinator.GetUnfinishedRequestCount() == 0,
				"Permanent capture errors fail a flexible batch instead of waiting indefinitely");
		}
		control.m_Availability = FrameCaptureAvailability::Busy;
		const uint64_t exact = coordinatorJobs.Admit({
			.m_RequiredProducts = { ImageCaptureProduct{} },
			.m_WaitForCapacity = true,
			.m_Sequence = FrameCaptureSequenceInfo{},
		});
		coordinator.BeginFrame(state);
		const auto exactFailure = coordinatorJobs.TakeResults();
		context.Check(exactFailure.size() == 1 && exactFailure[0].m_JobId == exact &&
			exactFailure[0].m_Status == CaptureJobStatus::Failed &&
			coordinator.GetUnfinishedRequestCount() == 0,
			"An exact sequence frame ignores flexible capacity waiting");

		control.m_Availability = FrameCaptureAvailability::Available;
		const uint64_t issued = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} }, .m_WaitForCapacity = true });
		coordinator.BeginFrame(state);
		if (control.m_Issued.size() == 2)
		{
			control.Fail(control.m_Issued.back().m_Id, "Submission failed.");
		}
		coordinator.Update();
		const auto submissionFailure = coordinatorJobs.TakeResults();
		++state.m_FrameSerial;
		coordinator.BeginFrame(state);
		context.Check(submissionFailure.size() == 1 && submissionFailure[0].m_JobId == issued &&
			submissionFailure[0].m_Status == CaptureJobStatus::Failed &&
			control.m_Issued.size() == 2 && coordinator.GetUnfinishedRequestCount() == 0,
			"An issued flexible capture fails once on submission failure and never substitutes a later frame");

		control.m_Availability = FrameCaptureAvailability::Busy;
		const uint64_t waiting = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} }, .m_WaitForCapacity = true });
		coordinator.BeginFrame(state);
		coordinator.PrepareForShutdown();
		coordinator.FinalizeAfterRenderHost();
		const auto shutdown = coordinatorJobs.TakeResults();
		context.Check(shutdown.size() == 1 && shutdown[0].m_JobId == waiting &&
			shutdown[0].m_Status == CaptureJobStatus::Cancelled &&
			coordinator.GetUnfinishedRequestCount() == 0,
			"Shutdown cancels a flexible capture still waiting for capacity");
	}

	void RunWriterQueueLimitTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-queue");
		const std::filesystem::path rejectedDirectory = directory.GetPath() / "rejected";
		FakeCaptureControl control;
		auto gate = std::make_shared<EncoderGate>();
		FrameCaptureCoordinator coordinator({
			.m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(),
			.m_ImageEncoder = [gate](const FrameCaptureImage& image) noexcept
			{
				std::unique_lock lock(gate->m_Mutex);
				gate->m_Entered = true;
				gate->m_Condition.notify_all();
				gate->m_Condition.wait(lock, [&gate]() { return gate->m_Released; });
				return EncodeTestPng(image);
			},
		});
		CaptureTestJobs coordinatorJobs(coordinator, context);

		std::vector<uint64_t> acceptedIds{ coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } }) };
		coordinator.BeginFrame(MakeFrameState(true));
		control.Complete(control.m_Issued[0].m_Id);
		coordinator.Update();
		{
			std::unique_lock lock(gate->m_Mutex);
			const bool entered = gate->m_Condition.wait_for(lock, std::chrono::seconds(10),
				[&gate]() { return gate->m_Entered; });
			context.Check(entered, "The running writer job is blocked before filling its queue");
		}

		for (uint32_t index = 0; index < 8; ++index)
		{
			acceptedIds.push_back(coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } }));
		}
		const std::array<uint64_t, 2> rejectedIds
		{
			coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} }, .m_OutputDirectory = rejectedDirectory }),
			coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} }, .m_OutputDirectory = rejectedDirectory }),
		};
		coordinator.BeginFrame(MakeFrameState(true));
		for (size_t index = 1; index < control.m_Issued.size(); ++index)
		{
			control.Complete(control.m_Issued[index].m_Id);
		}
		coordinator.Update();
		const auto rejected = coordinatorJobs.TakeResults();
		context.Check(rejected.size() == rejectedIds.size() &&
			rejected[0].m_JobId == rejectedIds[0] && rejected[1].m_JobId == rejectedIds[1] &&
			std::ranges::all_of(rejected, [](const CaptureJobResult& result)
				{
					return result.m_Status == CaptureJobStatus::Failed &&
						result.m_Products.front().m_Failure == "Capture capacity is unavailable before GPU submission." &&
						ProductFile(result, ".png").empty() && ProductFile(result, ".json").empty();
				}),
			"Reserved write slots reject excess requests before GPU capture with an explicit result");
		context.Check(coordinator.GetUnfinishedRequestCount() == 9 &&
			control.m_Issued.size() == 9 && !HasAnyFile(directory.GetPath()),
			"Only nine GPU captures are issued for one running writer and eight pending jobs");
		const uint64_t deferredId = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} }, .m_ReferenceViewId = "CAM_A", .m_WaitForCapacity = true });
		coordinator.BeginFrame(MakeFrameState(true));
		context.Check(!coordinator.GetPendingViewChange() && coordinatorJobs.TakeResults().empty() &&
			coordinator.GetUnfinishedRequestCount() == 10 && control.m_Issued.size() == 9,
			"A flexible batch waits without changing its camera while the real writer queue is full");

		{
			std::scoped_lock lock(gate->m_Mutex);
			gate->m_Released = true;
		}
		gate->m_Condition.notify_all();
		std::vector<CaptureJobResult> completed;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (completed.size() < acceptedIds.size() && std::chrono::steady_clock::now() < deadline)
		{
			coordinator.Update();
			for (CaptureJobResult& ready : coordinatorJobs.TakeResults())
			{
				completed.push_back(std::move(ready));
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		context.Check(completed.size() == acceptedIds.size() &&
			std::ranges::all_of(acceptedIds, [&completed](uint64_t id)
				{
					return std::ranges::count(completed, id, &CaptureJobResult::m_JobId) == 1;
				}) &&
			std::ranges::all_of(completed, [](const CaptureJobResult& result)
				{
					return result.m_Status == CaptureJobStatus::Completed &&
						HasPngSignature(ProductFile(result, ".png")) && !ReadText(ProductFile(result, ".json")).empty();
				}),
			"All accepted writer jobs publish their files and finish exactly once after release");

		const auto resumedView = coordinator.GetPendingViewChange();
		context.Check(resumedView && resumedView->m_JobId == deferredId,
			"Draining the writer queue makes its waiting batch view available again");
		coordinator.OnReferenceViewApplied(deferredId, true);
		coordinator.BeginFrame(MakeViewFrameState(1));
		control.Complete(control.m_Issued.back().m_Id);
		coordinator.PrepareForShutdown();
		coordinator.FinalizeAfterRenderHost();
		const auto resumed = coordinatorJobs.TakeResults();
		context.Check(resumed.size() == 1 && resumed[0].m_JobId == deferredId &&
			resumed[0].m_Status == CaptureJobStatus::Completed &&
			coordinator.GetUnfinishedRequestCount() == 0,
			"The writer accepts new captures after its pending queue drains");
		context.Check(!HasAnyFile(rejectedDirectory) && !HasPartialFile(directory.GetPath()),
			"Captures rejected by a full writer queue never write files after the writer resumes");
	}

	void RunAbandonedWritingTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("frame-capture-abandon");
		FakeCaptureControl control;
		auto gate = std::make_shared<EncoderGate>();
		std::vector<CaptureJobResult> results;
		std::chrono::steady_clock::duration finalizeTime{};
		uint64_t encodingId = 0;
		uint64_t queuedId = 0;
		{
			FrameCaptureCoordinator coordinator({
				.m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(),
				.m_ImageEncoder = [gate](const FrameCaptureImage& image) noexcept
				{
					std::unique_lock lock(gate->m_Mutex);
					gate->m_Entered = true;
					gate->m_Condition.notify_all();
					gate->m_Condition.wait(lock, [&gate]() { return gate->m_Released; });
					return EncodeTestPng(image);
				},
				.m_ShutdownWriteTimeout = std::chrono::milliseconds(200),
			});
			CaptureTestJobs coordinatorJobs(coordinator, context);
			encodingId = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} } });
			queuedId = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{ CaptureImageSource::Composited } } });
			coordinator.BeginFrame(MakeFrameState(true));
			control.Complete(control.m_Issued[0].m_Id);
			control.Complete(control.m_Issued[1].m_Id);
			coordinator.PrepareForShutdown();
			{
				std::unique_lock lock(gate->m_Mutex);
				gate->m_Condition.wait_for(lock, std::chrono::seconds(10),
					[&gate]() { return gate->m_Entered; });
			}

			const auto finalizeStart = std::chrono::steady_clock::now();
			coordinator.FinalizeAfterRenderHost();
			finalizeTime = std::chrono::steady_clock::now() - finalizeStart;
			for (CaptureJobResult& ready : coordinatorJobs.TakeResults())
			{
				results.push_back(std::move(ready));
			}
		}
		context.Check(results.size() == 2 &&
			std::ranges::all_of(results, [](const CaptureJobResult& result)
				{
					return result.m_Status == CaptureJobStatus::Failed &&
						result.m_Products.front().m_Failure.find("abandoned") != std::string::npos &&
						ProductFile(result, ".png").empty();
				}) &&
			results[0].m_JobId == encodingId && results[1].m_JobId == queuedId &&
			results[0].m_Products.front().m_Failure.find("stage: encoding") != std::string::npos &&
			results[1].m_Products.front().m_Failure.find("stage: queued") != std::string::npos &&
			finalizeTime < std::chrono::seconds(2),
			"Shutdown waits for blocked writing once in total, then abandons every unfinished capture");

		// The released writer finishes on its own; the encoder it owns is its
		// last reference to the gate besides this test's.
		{
			std::scoped_lock lock(gate->m_Mutex);
			gate->m_Released = true;
		}
		gate->m_Condition.notify_all();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (gate.use_count() > 1 && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		context.Check(gate.use_count() == 1 && !HasAnyFile(directory.GetPath()),
			"An abandoned capture publishes no files, even after its writing resumes");
	}

	void RunPublicationFaultTests(SelfTestContext& context) noexcept
	{
		for (const uint32_t failWrite : { 0u, 1u, 2u, 4u })
		{
			for (const uint32_t failPublish : { 0u, 1u, 2u, 4u })
			{
				if (failWrite == 0 && failPublish == 0)
				{
					continue;
				}
				TemporaryDirectory directory("capture-publication-fault");
				FakeCaptureControl control;
				auto files = std::make_shared<CapturePublicationTestFileSystem>();
				files->m_FailWrite = failWrite;
				files->m_FailPublish = failPublish;
				FrameCaptureCoordinator coordinator({ .m_Capture = &control,
					.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true,
					.m_PublicationFileSystem = files });
				CaptureTestJobs coordinatorJobs(coordinator, context);
				const auto id = coordinatorJobs.Admit({
					.m_RequiredProducts = { ResourceCaptureProduct{ { { .m_Resource = "First" }, { .m_Resource = "Second" } } }, GraphCaptureProduct{} },
				});
				coordinator.BeginFrame(MakeFrameState(true));
				control.Complete(control.m_Issued.front().m_Id);
				coordinator.Update();
				const auto results = coordinatorJobs.TakeResults();
				context.Check(results.size() == 1 && results[0].m_JobId == id &&
					results[0].m_Status == CaptureJobStatus::Failed &&
					ProductFiles(results[0], ".npy", CaptureProductKind::Resource).empty() && !results[0].m_Products.front().m_Failure.empty() &&
					!HasAnyFile(directory.GetPath()),
					"A temporary write or partial group publication failure withdraws every owned file");
				coordinator.Update();
				context.Check(coordinatorJobs.TakeResults().empty(), "A failed publication has exactly one terminal result");
			}
		}

		TemporaryDirectory directory("capture-publication-rollback");
		auto files = std::make_shared<CapturePublicationTestFileSystem>();
		files->m_FailPublish = 2;
		files->m_FailWithdraw = true;
		auto job = std::make_shared<CaptureWriteJob>();
		const std::string runIdentity = CreateCaptureRunIdentity();
		job->m_Bundle = { .m_RunIdentity = runIdentity,
			.m_Environment = SerializeCaptureEnvironment({}, runIdentity), .m_JobId = 1,
			.m_Attempt = 1, .m_ProductCount = 1,
			.m_Deliveries = { { .m_Input = { .m_Image = std::make_shared<FrameCaptureImage>(),
				.m_Submission = { 3, 1, 19, "Capture.TestAdapter" },
				.m_Metadata = { .m_RequestId = 1, .m_Backend = "dx12", .m_FrameSerial = 17 } } } } };
		job->m_Directory = directory.GetPath();
		job->m_BaseStem = "rollback";
		CaptureWriter writer(&EncodeTestPng, true, files);
		context.Check(writer.Submit(job), "The writer accepts the rollback-fault fixture");
		const auto receipt = job->GetReceipt();
		context.Check(receipt && receipt->m_Publication == CapturePublicationOutcome::Indeterminate &&
			receipt->m_Failure.find("indeterminate") != std::string::npos &&
			std::filesystem::exists(directory.GetPath() / "rollback.p0.png") &&
			!std::filesystem::exists(directory.GetPath() / "rollback.capture-bundle.json") &&
			!HasPartialFile(directory.GetPath()),
			"Failed rollback retains a typed Indeterminate receipt without a false completion marker");

		TemporaryDirectory throwingDirectory("capture-publication-serialization");
		CapturePublicationContent content;
		content.m_Files = { { .m_Suffix = ".data", .m_Bytes = { 1, 2, 3 } }, { .m_Suffix = ".json" } };
		uint32_t calls = 0;
		content.m_MakeMarker = [&](std::span<const std::filesystem::path>) -> std::string
			{
				if (++calls == 2)
				{
					throw std::runtime_error("Injected name-bound serialization failure.");
				}
				return "{}";
			};
		CapturePublicationControl publication;
		auto native = CreateCapturePublicationFileSystem();
		const auto failed = PublishCaptureFiles(content, throwingDirectory.GetPath(),
			"serialization", "unique", publication, *native);
		context.Check(calls == 2 && failed.m_Outcome == CapturePublicationOutcome::NotPublished &&
			failed.m_Failure.find("serialization failure") != std::string::npos &&
			!HasAnyFile(throwingDirectory.GetPath()),
			"A serializer exception after data publication rolls back the data and all temporary files");
	}

	void RunRenamePublicationTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("capture-publication-rename");
		FakeCaptureControl control;
		auto files = std::make_shared<CapturePublicationTestFileSystem>();
		files->m_RenameOnly = true;
		files->m_CollidePublish = 5;
		FrameCaptureCoordinator coordinator({ .m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true,
			.m_PublicationFileSystem = files });
		CaptureTestJobs coordinatorJobs(coordinator, context);
		GGLAB_UNUSED(coordinatorJobs.Admit({
			.m_RequiredProducts = { ResourceCaptureProduct{ { { .m_Resource = "First" }, { .m_Resource = "Second" } } }, GraphCaptureProduct{} },
			.m_Label = "renamed",
		}));
		coordinator.BeginFrame(MakeFrameState(true));
		control.Complete(control.m_Issued.front().m_Id);
		coordinator.Update();
		const auto results = coordinatorJobs.TakeResults();
		const bool complete = results.size() == 1 && results[0].m_Status == CaptureJobStatus::Completed;
		context.Check(complete && results[0].m_ManifestPath.filename().string().ends_with("-2.capture-bundle.json") &&
			ReadText(files->m_CollisionPath) == "existing capture" && !HasPartialFile(directory.GetPath()),
			"A final-manifest collision restores renamed data and retries the entire bundle without overwrite");
		if (complete)
		{
			const auto metadata = nlohmann::json::parse(ReadText(ProductFile(results[0], ".json")));
			context.Check(metadata["targets"][0]["array"] ==
				GetFrameCapturePathText(ProductFiles(results[0], ".npy", CaptureProductKind::Resource)[0].filename()) &&
				metadata["targets"][1]["array"] ==
				GetFrameCapturePathText(ProductFiles(results[0], ".npy", CaptureProductKind::Resource)[1].filename()) &&
				metadata["graph"] == GetFrameCapturePathText(ProductFile(results[0], ".json", CaptureProductKind::Graph).filename()),
				"Name-bound array and graph metadata use the final collision-resolved paths");
		}
	}

	void RunPublicationShutdownTests(SelfTestContext& context) noexcept
	{
		for (const auto& product : std::vector<CaptureProductSpec>{ ImageCaptureProduct{}, GraphCaptureProduct{}, ResourceCaptureProduct{ { { .m_Resource = "Values" } } } })
		{
			const auto source = GetCaptureProductSource(product);
			for (const auto block : { CapturePublicationTestFileSystem::Block::TemporaryWrite,
				CapturePublicationTestFileSystem::Block::Publication })
			{
				TemporaryDirectory directory("capture-publication-shutdown");
				FakeCaptureControl control;
				auto files = std::make_shared<CapturePublicationTestFileSystem>();
				files->m_Block = block;
				const bool claimed = block == CapturePublicationTestFileSystem::Block::Publication;
				std::vector<CaptureJobResult> results;
				{
					FrameCaptureCoordinator coordinator({ .m_Capture = &control,
						.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
						.m_ShutdownWriteTimeout = std::chrono::milliseconds(100), .m_PublicationFileSystem = files });
					CaptureTestJobs coordinatorJobs(coordinator, context);
					CapturePlan plan{
						.m_RequiredProducts = { product },
					};
					if (source == FrameCaptureSource::ResourceExport)
					{
						plan.m_RequiredProducts.emplace_back(GraphCaptureProduct{});
					}
					const auto id = coordinatorJobs.Admit(std::move(plan));
					coordinator.BeginFrame(MakeFrameState(true));
					control.Complete(control.m_Issued.front().m_Id);
					coordinator.Update();
					context.Check(files->WaitEntered() && coordinatorJobs.TakeResults().empty() &&
						coordinator.GetJobSnapshot(id)->m_Stage == CaptureJobStage::Encoding,
						"A blocked file writer leaves issued work unfinished and retains its Encoding job stage");
					if (source == FrameCaptureSource::Scene)
					{
						context.Check(!control.m_LastImage.expired(), "The blocked writer retains the shared CPU image");
					}
					coordinator.PrepareForShutdown();
					const auto start = std::chrono::steady_clock::now();
					coordinator.FinalizeAfterRenderHost();
					results = coordinatorJobs.TakeResults();
					context.Check(std::chrono::steady_clock::now() - start < std::chrono::seconds(2) &&
						results.size() == 1 && results[0].m_JobId == id &&
						results[0].m_Status == CaptureJobStatus::Failed &&
						results[0].m_Products.front().m_Failure.find(claimed ? "indeterminate" : "abandoned") != std::string::npos &&
						ProductFile(results[0], ".json").empty() && coordinator.GetUnfinishedRequestCount() == 0,
						"Shutdown distinguishes publication claim from completion under one bounded wait");
					coordinator.Update();
					context.Check(coordinatorJobs.TakeResults().empty(), "Repeated collection cannot duplicate the timeout result");
				}
				context.Check(source != FrameCaptureSource::Scene || !control.m_LastImage.expired(),
					"Destroying the coordinator does not destroy products held by its detached writer");
				files->Release();
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
				while (files.use_count() > 1 && std::chrono::steady_clock::now() < deadline)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				context.Check(files.use_count() == 1 && !HasPartialFile(directory.GetPath()) &&
					HasAnyFile(directory.GetPath()) == claimed && control.m_LastImage.expired(),
					"A detached writer drains safely: abandoned files never publish, while claimed publication may finish late");
			}
		}
	}
}

namespace gglab
{
	using namespace capture_test;

	void RunCapturePublicationSelfTests(SelfTestContext& context) noexcept
	{
		RunNameCollisionTests(context);
		RunWriterThreadTests(context);
		RunCapacityAdmissionTests(context);
		RunDeferredCapacityTests(context);
		RunWriterQueueLimitTests(context);
		RunAbandonedWritingTests(context);
		RunPublicationFaultTests(context);
		RunRenamePublicationTests(context);
		RunPublicationShutdownTests(context);
	}
}
