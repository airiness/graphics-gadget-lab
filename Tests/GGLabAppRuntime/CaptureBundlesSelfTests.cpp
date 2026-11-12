#include "Capture/CaptureBundle.h"
#include "Capture/CaptureEnvironment.h"
#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "Capture/CaptureProductEncoder.h"
#include "Capture/CapturePublisher.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/NonFiniteWatchCoordinator.h"
#include "CaptureCoordinatorTestCases.h"
#include "CapturePublicationTestFileSystem.h"
#include "CaptureTestServices.h"
#include "FrameCaptureCoordinatorSelfTests.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchTypes.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RHI/RHIDeviceIdentity.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabTestCore/SelfTest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <nlohmann/json.hpp>
#include <ranges>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gglab::capture_test
{
	void RunBundleProvenanceTests(SelfTestContext& context) noexcept
	{
		CaptureProductEncodeInput image{
			.m_Image = std::make_shared<FrameCaptureImage>(),
			.m_Submission = { 1, 1, 3, "test-adapter" },
		};
		image.m_Metadata.m_Backend = "dx12";
		image.m_Metadata.m_FrameSerial = 17;
		const std::string run = "0123456789abcdef0123456789abcdef";
		CaptureBundleInput input{ .m_RunIdentity = run,
			.m_Environment = SerializeCaptureEnvironment({}, run),
			.m_JobId = 1, .m_Attempt = 2, .m_ProductCount = 2,
			.m_Deliveries = { { .m_ProductIndex = 0, .m_Input = image },
				{ .m_ProductIndex = 1, .m_Input = image } } };
		std::atomic<CaptureWriteStage> stage = CaptureWriteStage::Queued;
		const auto valid = EncodeCaptureBundle(input, &EncodeTestPng, stage);
		context.Check(valid.m_Failure.empty() && valid.m_Content.m_Files.size() == 5 &&
			valid.m_Products.size() == 2, "Matched completed image products form one required bundle");
		for (uint32_t change = 0; change < 12; ++change)
		{
			auto invalid = input;
			auto& second = invalid.m_Deliveries[1];
			switch (change)
			{
			case 0:
				++second.m_Input.m_Metadata.m_FrameSerial;
				break;
			case 1:
				second.m_Input.m_Metadata.m_Backend = "vulkan";
				break;
			case 2:
				++second.m_Input.m_Submission.m_FenceValue;
				break;
			case 3:
				second.m_Input.m_Submission.m_AdapterIdentity = "other-adapter";
				break;
			case 4:
				second.m_Input.m_Submission.m_FenceValue = 0;
				break;
			case 5:
				second.m_Input.m_Image.reset();
				break;
			case 6:
				second.m_ProductIndex = 0;
				break;
			case 7:
				invalid.m_Deliveries.pop_back();
				break;
			case 8:
				invalid.m_RunIdentity = "old-local-id";
				break;
			case 9:
				second.m_GraphProductIndex = 0;
				break;
			case 10:
				invalid.m_Environment.clear();
				break;
			case 11:
				invalid.m_Environment = "[]";
				break;
			}
			const auto rejected = EncodeCaptureBundle(invalid, &EncodeTestPng, stage);
			context.Check(!rejected.m_Failure.empty() && rejected.m_Content.m_Files.empty(),
				"Inconsistent provenance, incomplete products, unqualified identity or a missing environment cannot reach publication");
		}
		NonFiniteWatchCoverage coverage{ .m_ConfigurationVersion = 1,
			.m_ProductionRequested = true, .m_InstrumentationAvailable = true,
			.m_FirstFrameSerial = 17, .m_LastFrameSerial = 17, .m_FrameCount = 1,
			.m_CompletedFrames = 1, .m_Complete = true,
			.m_Production = { NonFiniteClaimStatus::Failed, NonFiniteWatchFailure::NonFiniteReplaced } };
		for (uint32_t change = 0; change < 6; ++change)
		{
			auto watched = input;
			auto claim = coverage;
			if (change == 1)
			{
				claim.m_Mode = NonFiniteWatchMode::Fail;
			}
			else if (change == 2)
			{
				claim.m_Complete = false;
			}
			else if (change == 3)
			{
				claim.m_FirstFrameSerial = claim.m_LastFrameSerial = 18;
			}
			else if (change == 5)
			{
				claim.m_Mode = NonFiniteWatchMode::Fail;
				claim.m_Production = { NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None };
			}
			for (auto& delivery : watched.m_Deliveries)
			{
				delivery.m_Input.m_Metadata.m_WatchCoverage = std::make_shared<const NonFiniteWatchCoverage>(claim);
			}
			if (change == 4)
			{
				watched.m_Deliveries[1].m_Input.m_Metadata.m_WatchCoverage.reset();
			}
			const auto encoded = EncodeCaptureBundle(watched, &EncodeTestPng, stage);
			const bool accepted = change == 0 || change == 5;
			context.Check(encoded.m_Failure.empty() == accepted,
				"Bundles preserve report/fail claim semantics and require complete identical submitted-frame coverage");
		}
	}

	void RunCaptureEnvironmentTests(SelfTestContext& context) noexcept
	{
		using Json = nlohmann::json;
		const std::string run = "0123456789abcdef0123456789abcdef";
		const auto unknown = Json::parse(SerializeCaptureEnvironment({}, run));
		context.Check(unknown["schemaVersion"] == CaptureEnvironmentSchemaVersion &&
			unknown["build"]["source"] == "unavailable" && unknown["build"]["revision"].is_null() &&
			unknown["build"]["dirty"].is_null() && unknown["build"]["pixIntegration"].is_null() &&
			unknown["process"]["runIdentity"] == run && unknown["process"]["startedAtUtc"].is_null() &&
			unknown["process"]["os"].is_null() && unknown["device"].is_null() &&
			unknown["validation"]["requested"].is_null() && unknown["validation"]["effective"].is_null(),
			"An environment without build or device facts reports them as null rather than estimating them");

		using namespace std::chrono;
		CaptureEnvironment environment{
			.m_Host = {
				.m_Build = { .m_Available = true, .m_Revision = std::string(40, 'a'), .m_Dirty = true,
					.m_Configuration = "Release", .m_Compiler = "MSVC 19.51.36231" },
				.m_OperatingSystem = "Windows 10.0.26200",
				.m_Presentation = "hidden",
			},
			.m_Device = { .m_Available = true, .m_Backend = RHIBackendType::Vulkan, .m_AdapterName = "Adapter",
				.m_VendorId = 0x10de, .m_DeviceId = 2, .m_DriverVersion = "581.57",
				.m_DriverVersionRaw = "0x9150c000", .m_ApiVersion = "1.3.290",
				.m_CompatibilityIdentity = "vulkan:test", .m_RequestedValidation = RHIValidationLevel::Standard,
				.m_EffectiveValidation = RHIValidationLevel::Off, .m_PixIntegration = true },
			.m_StartedAt = sys_days{ year{ 2026 } / 10 / 21 } + hours{ 12 } + seconds{ 5 },
		};
		const auto known = Json::parse(SerializeCaptureEnvironment(environment, run));
		context.Check(known["build"]["source"] == "build-stamp" &&
			known["build"]["revision"] == std::string(40, 'a') && known["build"]["dirty"] == true &&
			known["build"]["pixIntegration"] == true && known["process"]["startedAtUtc"] == "2026-10-21T12:00:05Z" &&
			known["process"]["os"] == "Windows 10.0.26200" && known["process"]["host"] == "hidden" &&
			known["device"]["backend"] == "vulkan" && known["device"]["driverVersion"] == "581.57" &&
			known["device"]["subsystemId"].is_null() && known["device"]["dedicatedVideoMemoryBytes"].is_null() &&
			known["device"]["api"] == Json{ { "apiVersion", "1.3.290" } } &&
			known["device"]["compatibilityIdentity"] == "vulkan:test" &&
			known["validation"]["requested"] == "standard" && known["validation"]["effective"] == "off",
			"An environment records build, process, device and requested/effective validation facts as given");

		environment.m_Host.m_Build.m_Available = false;
		const auto withdrawn = Json::parse(SerializeCaptureEnvironment(environment, run));
		context.Check(withdrawn["build"]["source"] == "unavailable" && withdrawn["build"]["revision"].is_null() &&
			withdrawn["build"]["dirty"].is_null() && withdrawn["build"]["configuration"] == "Release",
			"An unavailable build stamp never reports a revision or dirty state");
	}

	void RunBundlePublicationTests(SelfTestContext& context) noexcept
	{
		for (const bool publicationFault : { false, true })
		{
			for (uint32_t position = 1; position <= 6; ++position)
			{
				TemporaryDirectory directory("capture-bundle-fault");
				FakeCaptureControl control;
				auto files = std::make_shared<CapturePublicationTestFileSystem>();
				files->m_FailWrite = publicationFault ? 0 : position;
				files->m_FailPublish = publicationFault ? position : 0;
				FrameCaptureCoordinator coordinator({ .m_Capture = &control,
					.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
					.m_WriteOnCallingThread = true, .m_PublicationFileSystem = files });
				const auto admission = coordinator.TrySubmit(MakeRequiredTestPlan());
				coordinator.BeginFrame(MakeFrameState(true));
				for (const auto& issued : control.m_Issued)
				{
					control.Complete(issued.m_Id);
				}
				coordinator.Update();
				const auto result = coordinator.TakeJobResult(admission.m_JobId);
				context.Check(result && result->m_Status == CaptureJobStatus::Failed &&
					result->m_Publication == CaptureJobPublication::NotPublished && result->m_ManifestPath.empty() &&
					(publicationFault ? files->GetPublishCount() : files->GetWriteCount()) == position &&
					std::ranges::all_of(result->m_Products, [](const auto& product)
					{
						return product.m_Status == CaptureJobStatus::Failed && product.m_Files.empty();
					}) && !HasAnyFile(directory.GetPath()) && !HasPartialFile(directory.GetPath()),
					"Every data, sidecar and final-manifest file fault rolls back the required bundle and leaves no completion marker");
				coordinator.FinalizeAfterRenderHost();
			}
		}
		for (uint32_t collision = 1; collision <= 6; ++collision)
		{
			TemporaryDirectory directory("capture-bundle-collision");
			FakeCaptureControl control;
			auto files = std::make_shared<CapturePublicationTestFileSystem>();
			files->m_CollidePublish = collision;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
				.m_WriteOnCallingThread = true, .m_PublicationFileSystem = files });
			const auto admission = coordinator.TrySubmit(MakeRequiredTestPlan());
			coordinator.BeginFrame(MakeFrameState(true));
			for (const auto& issued : control.m_Issued)
			{
				control.Complete(issued.m_Id);
			}
			coordinator.Update();
			const auto result = coordinator.TakeJobResult(admission.m_JobId);
			context.Check(result && result->m_Status == CaptureJobStatus::Completed &&
				files->GetPublishCount() == collision + 6 &&
				ReadText(files->m_CollisionPath) == "existing capture" && !HasPartialFile(directory.GetPath()),
				"A collision at any bundle file preserves existing content and retries the complete group");
			if (result && !result->m_ManifestPath.empty())
			{
				const auto manifest = nlohmann::json::parse(ReadText(result->m_ManifestPath));
				bool bound = manifest["bundleId"] == result->m_BundleId && manifest["schemaVersion"] == 3 &&
					manifest["schema"] == "gglab.capture-bundle" && manifest["products"].size() == 3 &&
					manifest["environment"]["schemaVersion"] == 1 &&
					manifest["environment"]["process"]["runIdentity"] == manifest["runIdentity"];
				for (const auto& product : manifest["products"])
				{
					const auto metadataName = product["metadata"].get<std::string>();
					const auto metadata = nlohmann::json::parse(ReadText(directory.GetPath() /
						std::filesystem::path(std::u8string(metadataName.begin(), metadataName.end()))));
					bound &= metadata["schemaVersion"] == product["schemaVersion"];
					for (const auto& name : product["files"])
					{
						const auto filename = name.get<std::string>();
						bound &= std::filesystem::is_regular_file(directory.GetPath() /
							std::filesystem::path(std::u8string(filename.begin(), filename.end())));
					}
					if (product["source"] == "scene")
					{
						bound &= metadata["image"]["file"] == product["files"][0];
					}
					if (product["source"] == "resource-export")
					{
						bound &= metadata["targets"][0]["array"] == product["files"][0];
					}
				}
				context.Check(bound, "Retried bundle manifests and every path-bound sidecar reference the final candidate names");
			}
			coordinator.FinalizeAfterRenderHost();
		}
		TemporaryDirectory directory("capture-bundle-rollback");
		FakeCaptureControl control;
		auto files = std::make_shared<CapturePublicationTestFileSystem>();
		files->m_FailPublish = 6;
		files->m_FailWithdraw = true;
		FrameCaptureCoordinator coordinator({ .m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
			.m_WriteOnCallingThread = true, .m_PublicationFileSystem = files });
		const auto admission = coordinator.TrySubmit(MakeRequiredTestPlan());
		coordinator.BeginFrame(MakeFrameState(true));
		for (const auto& issued : control.m_Issued)
		{
			control.Complete(issued.m_Id);
		}
		coordinator.Update();
		const auto result = coordinator.TakeJobResult(admission.m_JobId);
		context.Check(result && result->m_Publication == CaptureJobPublication::Indeterminate &&
			result->m_ManifestPath.empty() && HasAnyFile(directory.GetPath()) &&
			std::ranges::none_of(std::filesystem::directory_iterator(directory.GetPath()), [](const auto& file)
			{
				return file.path().filename().string().ends_with(".capture-bundle.json");
			}), "Rollback failure reports indeterminate orphan files without claiming a complete bundle");
		coordinator.FinalizeAfterRenderHost();
	}

	void RunBundleSingleProductTests(SelfTestContext& context) noexcept
	{
		for (const auto product : std::vector<CaptureProductSpec>{ ImageCaptureProduct{},
			ImageCaptureProduct{ CaptureImageSource::Composited },
			DiagnosticCaptureProduct{ PostProcessDebugTap::TemporalHistoryWeight }, GraphCaptureProduct{},
			ResourceCaptureProduct{ { { .m_Resource = "Values" } } } })
		{
			TemporaryDirectory directory("capture-bundle-single");
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath() / std::filesystem::path(u8"\u4e2d\u6587 \U0001F9EA"),
				.m_ImageEncoder = &EncodeTestPng, .m_WriteOnCallingThread = true });
			const auto admission = coordinator.TrySubmit({ .m_RequiredProducts = { product } });
			coordinator.BeginFrame(MakeFrameState(true));
			control.Complete(control.m_Issued[0].m_Id);
			coordinator.Update();
			const auto result = coordinator.TakeJobResult(admission.m_JobId);
			context.Check(result && result->m_Status == CaptureJobStatus::Completed &&
				result->m_Products.size() == 1 && std::filesystem::exists(result->m_ManifestPath),
				"Every single-product operation uses the same final bundle marker, including Unicode output roots");
			if (result && !result->m_ManifestPath.empty())
			{
				const auto manifest = nlohmann::json::parse(ReadText(result->m_ManifestPath));
				context.Check(manifest["products"][0]["source"].get<std::string>() == GetFrameCaptureSourceName(GetCaptureProductSource(product)) &&
					manifest["provenance"]["frame"]["serial"] == 17 && manifest["jobId"] == admission.m_JobId &&
					manifest["runIdentity"].get<std::string>().size() == 32 && manifest["attempt"] == 1,
					"Bundle identity qualifies the logical job/attempt and product records preserve the requested source");
			}
			coordinator.FinalizeAfterRenderHost();
		}
	}

	void RunBundleManifestShutdownTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("capture-bundle-manifest-shutdown");
		FakeCaptureControl control;
		auto files = std::make_shared<CapturePublicationTestFileSystem>();
		files->m_Block = CapturePublicationTestFileSystem::Block::Publication;
		files->m_BlockPublication = 6;
		{
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
				.m_ShutdownWriteTimeout = std::chrono::milliseconds(50), .m_PublicationFileSystem = files });
			const auto admission = coordinator.TrySubmit(MakeRequiredTestPlan());
			coordinator.BeginFrame(MakeFrameState(true));
			for (const auto& issued : control.m_Issued)
			{
				control.Complete(issued.m_Id);
			}
			coordinator.Update();
			context.Check(files->WaitEntered() && !coordinator.TakeJobResult(admission.m_JobId) &&
				HasAnyFile(directory.GetPath()) && coordinator.GetJobSnapshot(admission.m_JobId)->m_FinishedProducts == 0,
				"Published product files cannot complete a job while its final manifest is blocked");
			coordinator.FinalizeAfterRenderHost();
			const auto result = coordinator.TakeJobResult(admission.m_JobId);
			context.Check(result && result->m_Publication == CaptureJobPublication::Indeterminate &&
				!result->m_BundleId.empty() && result->m_ManifestPath.empty() &&
				std::ranges::all_of(result->m_Products, [](const auto& product)
				{
					return product.m_Status == CaptureJobStatus::Failed && product.m_Publication == CaptureJobPublication::Indeterminate;
				}), "One timeout snapshot settles all required products consistently after the shared publication claim");
			files->Release();
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (files.use_count() > 1 && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::yield();
		}
		context.Check(files.use_count() == 1 && !HasPartialFile(directory.GetPath()) &&
			std::ranges::any_of(std::filesystem::directory_iterator(directory.GetPath()), [](const auto& file)
			{
				return file.path().filename().string().ends_with(".capture-bundle.json");
			}), "A claimed detached bundle may publish its manifest late without rewriting the terminal result");
	}

	void RunRequiredPlanLifecycleTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("required-plan-lifecycle");
		{
			FakeCaptureControl control;
			control.m_ResourceFrameAvailability = FrameCaptureAvailability::Busy;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
				.m_WriteOnCallingThread = true });
			CaptureTestJobs coordinatorJobs(coordinator, context);
			auto plan = MakeRequiredTestPlan();
			plan.m_Sequence = FrameCaptureSequenceInfo{ .m_SequenceId = 91, .m_Frame = 3 };
			const auto first = coordinatorJobs.Admit(plan);
			const auto second = coordinatorJobs.Admit(plan);
			const auto independent = coordinatorJobs.Admit({ .m_RequiredProducts = { ImageCaptureProduct{} }, .m_Sequence = plan.m_Sequence });
			context.Check((coordinator.CancelJob(first) == CaptureCancelResult::Cancelled) && coordinator.GetUnfinishedRequestCount() == 3,
				"Cancelling an explicit plan does not reconstruct membership from identical sequence metadata");
			const auto cancelled = coordinatorJobs.TakeResults();
			context.Check(cancelled.size() == 1 && cancelled[0].m_Products.size() == 3 && cancelled[0].m_JobId == first &&
				cancelled[0].m_JobId == first && (coordinator.CancelJob(independent) == CaptureCancelResult::Cancelled) &&
				coordinatorJobs.TakeResults().size() == 1 && coordinator.GetUnfinishedRequestCount() == 2,
				"A single-product job remains independent of an explicit plan with the same provenance");
			control.m_ResourceFrameAvailability = FrameCaptureAvailability::Available;
			coordinator.BeginFrame(MakeFrameState(true));
			for (const auto& issued : control.m_Issued)
			{
				control.Complete(issued.m_Id);
			}
			coordinator.OnFrameSubmitted();
			coordinator.Update();
			const auto completed = coordinatorJobs.TakeResults();
			context.Check(completed.size() == 1 && completed[0].m_Products.size() == 3 && completed[0].m_JobId == second &&
				completed[0].m_JobId == second && control.m_Issued.size() == 2 &&
				control.m_Issued[0].m_GroupId == second && control.m_Issued[1].m_GroupId == second,
				"Only the surviving plan issues, with its own stable Runtime association and one terminal job receipt");
			const auto view = coordinatorJobs.Admit({
				.m_RequiredProducts = { ImageCaptureProduct{} },
				.m_ReferenceViewId = "CAM_A" });
			const auto change = coordinator.GetPendingViewChange();
			context.Check(change && change->m_JobId == view,
				"A view change carries job identity after earlier multi-product deliveries");
			coordinator.OnReferenceViewApplied(view, true);
			auto state = MakeFrameState(true);
			++state.m_FrameSerial;
			coordinator.BeginFrame(state);
			coordinator.OnFrameSubmitted();
			control.Complete(control.m_Issued.back().m_Id);
			coordinator.Update();
			const auto restored = coordinatorJobs.TakeResults();
			context.Check(restored.size() == 1 && restored[0].m_JobId == view &&
				restored[0].m_Status == CaptureJobStatus::Completed &&
				restored[0].m_Products.front().m_Metadata &&
				restored[0].m_Products.front().m_Metadata->m_RequestId != view &&
				restored[0].m_Products.front().m_Metadata->m_Camera.m_ReferenceViewId == "CAM_A",
				"Applying a view uses the job ID while its saved artifact keeps independent provenance");
			coordinator.FinalizeAfterRenderHost();
		}
		for (const bool missingGraph : { false, true })
		{
			TemporaryDirectory failedDirectory("required-plan-transport-failure");
			FakeCaptureControl control;
			uint32_t encodes = 0;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = failedDirectory.GetPath(),
				.m_ImageEncoder = [&](const FrameCaptureImage& image)
				{
					++encodes;
					return EncodeTestPng(image);
				},
				.m_WriteOnCallingThread = true });
			CaptureTestJobs coordinatorJobs(coordinator, context);
			const auto ids = coordinatorJobs.Admit(MakeRequiredTestPlan());
			coordinator.BeginFrame(MakeFrameState(true));
			coordinator.OnFrameSubmitted();
			control.Complete(control.m_Issued[0].m_Id);
			coordinator.Update();
			const auto image = control.m_LastImage;
			context.Check(coordinatorJobs.TakeResults().empty() && encodes == 0 && !image.expired() &&
				!std::filesystem::exists(failedDirectory.GetPath()),
				"A ready image retains its product but cannot publish before the required arrays and graph arrive");
			if (missingGraph)
			{
				control.m_OmitGraph = true;
				control.Complete(control.m_Issued[1].m_Id);
			}
			else
			{
				control.Fail(control.m_Issued[1].m_Id, "Required resource transport failed.");
			}
			coordinator.Update();
			const auto failed = coordinatorJobs.TakeResults();
			context.Check(failed.size() == 1 && failed[0].m_Products.size() == 3 && failed[0].m_JobId == ids && failed[0].m_JobId == ids &&
				std::ranges::all_of(failed, [](const auto& result)
				{
					return result.m_Status == CaptureJobStatus::Failed && ProductFile(result, ".png").empty() &&
						ProductFiles(result, ".npy", CaptureProductKind::Resource).empty() && !result.m_Products.front().m_Failure.empty();
				}) && encodes == 0 && image.expired() && coordinator.GetUnfinishedRequestCount() == 0 &&
				control.GetUnfinishedRequestCount() == 0 && !std::filesystem::exists(failedDirectory.GetPath()),
				"Missing required graph or failed resource transport fails every output "
				"without partial publication or retained products");
			coordinator.FinalizeAfterRenderHost();
		}
		{
			FakeCaptureControl control;
			control.m_RejectAfterIssued = 1;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
				.m_WriteOnCallingThread = true });
			CaptureTestJobs coordinatorJobs(coordinator, context);
			GGLAB_UNUSED(coordinatorJobs.Admit(MakeRequiredTestPlan()));
			coordinator.BeginFrame(MakeFrameState(true));
			const auto failed = coordinatorJobs.TakeResults();
			context.Check(failed.size() == 1 && failed[0].m_Products.size() == 3 && failed[0].m_Status == CaptureJobStatus::Failed &&
				failed[0].m_Products[1].m_Status == CaptureJobStatus::Failed && failed[0].m_Products.front().m_Metadata &&
				!failed[0].m_Products[1].m_Metadata && control.m_Issued.size() == 1,
				"An unexpected rejection after advisory admission fails all outputs, including the accepted sibling");
			control.Complete(control.m_Issued[0].m_Id);
			coordinator.Update();
			context.Check(coordinatorJobs.TakeResults().empty() && control.GetUnfinishedRequestCount() == 0 &&
				coordinator.GetCaptureAvailability({ .m_RequiredProducts = std::vector<CaptureProductSpec>(9, ImageCaptureProduct{}) }) == FrameCaptureAvailability::Available,
				"The rejected plan drains its late Runtime product without another result or an orphan writer reservation");
			coordinator.FinalizeAfterRenderHost();
		}
	}

	void RunRequiredPlanRetryTests(SelfTestContext& context) noexcept
	{
		for (const auto mode : { NonFiniteWatchMode::Report, NonFiniteWatchMode::Fail })
		{
			for (const bool diagnostic : { false, true })
			{
				for (const size_t busyIndex : { 0u, 1u })
				{
					TemporaryDirectory directory("required-plan-retry");
					FakeCaptureControl control;
					control.m_Instrumentation = true;
					NonFiniteWatchCoordinator watch(&control);
					std::string error;
					context.Check(watch.Configure({ .m_Mode = mode, .m_Boundary = false }, error),
						"A required-plan retry configures its existing production watch mode");
					FrameCaptureCoordinator coordinator({ .m_Capture = &control,
						.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
						.m_WriteOnCallingThread = true, .m_NonFiniteWatch = &watch });
					CaptureTestJobs coordinatorJobs(coordinator, context);
					auto plan = MakeRequiredTestPlan();
					if (diagnostic)
					{
						plan.m_RequiredProducts[0] = DiagnosticCaptureProduct{ PostProcessDebugTap::TemporalHistoryWeight };
					}
					const auto ids = coordinatorJobs.Admit(std::move(plan));
					auto state = MakeFrameState(true);
					coordinator.BeginFrame(state);
					for (uint32_t retry = 0; retry < 3; ++retry)
					{
						const auto first = control.m_Issued.size() - 2;
						const auto staleSibling = control.m_Issued[first + 1 - busyIndex].m_Id;
						control.Busy(control.m_Issued[first + busyIndex].m_Id);
						coordinator.Update();
						context.Check(coordinatorJobs.TakeResults().empty() && coordinator.GetUnfinishedRequestCount() == 2 &&
							coordinator.GetCaptureAvailability({ .m_RequiredProducts = std::vector<CaptureProductSpec>(9, ImageCaptureProduct{}) }) ==
								FrameCaptureAvailability::Available,
							"One Busy result retires the complete required-product attempt "
							"and returns every writer reservation");
						++state.m_FrameSerial;
						++state.m_FrameIndex;
						state.m_TotalTime += 0.25;
						state.m_Camera.m_Name = std::format("Retried camera {}", retry);
						coordinator.BeginFrame(state);
						control.Complete(staleSibling);
						coordinator.Update();
						context.Check(coordinatorJobs.TakeResults().empty() && !std::filesystem::exists(directory.GetPath()) &&
							control.m_Issued[first + 2].m_GroupId == ids &&
							control.m_Issued[first + 3].m_GroupId == ids,
							"A late sibling from a discarded attempt cannot publish into the freshly bound plan");
					}
					coordinator.SetFrameSceneInputs(state.m_FrameSerial, { .m_LocalLightCount = 99 });
					watch.OnFrameSubmitted(state.m_FrameSerial);
					coordinator.OnFrameSubmitted();
					const auto final = control.m_Issued.size() - 2;
					control.Complete(control.m_Issued[final].m_Id);
					coordinator.Update();
					context.Check(coordinatorJobs.TakeResults().empty() && !std::filesystem::exists(directory.GetPath()),
						"A retried image waits for every other required product and the final attempt's coverage");
					control.Complete(control.m_Issued[final + 1].m_Id);
					coordinator.Update();
					context.Check(coordinatorJobs.TakeResults().empty() && !std::filesystem::exists(directory.GetPath()),
						"Ready image, arrays and graph do not bypass missing required watch evidence");
					auto production = std::make_shared<NonFiniteProductionFrame>();
					production->m_FrameSerial = state.m_FrameSerial;
					production->m_Claim = { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None };
					control.m_Production.push_back(std::move(production));
					coordinator.Update();
					const auto completed = coordinatorJobs.TakeResults();
					context.Check(completed.size() == 1 && completed[0].m_Products.size() == 3 && std::ranges::all_of(completed[0].m_Products, [&](const auto& result)
						{
							const auto& metadata = result.m_Metadata;
							return result.m_Status == CaptureJobStatus::Completed && metadata &&
								metadata->m_FrameSerial == state.m_FrameSerial &&
								metadata->m_FrameIndex == state.m_FrameIndex &&
								metadata->m_TotalTime == state.m_TotalTime &&
								metadata->m_Camera.m_Name == state.m_Camera.m_Name &&
								metadata->m_SceneInputs && metadata->m_SceneInputs->m_LocalLightCount == 99 &&
								metadata->m_WatchCoverage && metadata->m_WatchCoverage->m_Complete &&
								metadata->m_WatchCoverage->m_FrameCount == 1 &&
								metadata->m_WatchCoverage->m_FirstFrameSerial == state.m_FrameSerial;
						}) && completed[0].m_Products.front().m_Metadata->m_WatchCoverage == completed[0].m_Products[1].m_Metadata->m_WatchCoverage,
						"All required products share fresh actual-frame provenance and one coverage snapshot "
						"excluding discarded attempts");
					if (completed.size() == 1 && completed[0].m_Products.size() == 3 && completed[0].m_Status == CaptureJobStatus::Completed &&
						completed[0].m_Status == CaptureJobStatus::Completed)
					{
						const auto image = nlohmann::json::parse(ReadText(ProductFile(completed[0], ".json")));
						const auto resources = nlohmann::json::parse(ReadText(ProductFile(completed[0], ".json", CaptureProductKind::Resource)));
						const auto graph = nlohmann::json::parse(ReadText(ProductFile(completed[0], ".json", CaptureProductKind::Graph)));
						context.Check(image["frame"] == resources["frame"]["frame"] &&
							image["frame"] == graph["frame"]["frame"] &&
							image["camera"] == resources["frame"]["camera"] && image["time"] == graph["frame"]["time"] &&
							image["watch"] == resources["frame"]["watch"] && image["watch"] == graph["frame"]["watch"],
							"Published PNG sidecar, resource metadata and graph agree on actual frame, "
							"camera, time and watch coverage");
					}
					coordinator.FinalizeAfterRenderHost();
					context.Check(coordinatorJobs.TakeResults().empty() && coordinator.GetUnfinishedRequestCount() == 0 &&
						control.GetUnfinishedRequestCount() == 0 && control.m_LastImage.expired(),
						"Repeated whole-plan retries leave no duplicate terminal result, "
						"live Runtime request or retained CPU product");
				}
			}
		}
	}

	void RunRequiredPlanWatchTests(SelfTestContext& context) noexcept
	{
		for (const auto mode : { NonFiniteWatchMode::Report, NonFiniteWatchMode::Fail })
		{
			TemporaryDirectory directory("required-plan-watch");
			FakeCaptureControl control;
			control.m_Instrumentation = true;
			NonFiniteWatchCoordinator watch(&control);
			std::string error;
			context.Check(watch.Configure({ .m_Mode = mode, .m_Boundary = false }, error),
				"Required products retain report/fail watch configuration");
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(), .m_ImageEncoder = &EncodeTestPng,
				.m_WriteOnCallingThread = true, .m_NonFiniteWatch = &watch });
			CaptureTestJobs coordinatorJobs(coordinator, context);
			GGLAB_UNUSED(coordinatorJobs.Admit(MakeRequiredTestPlan()));
			const auto state = MakeFrameState(true);
			coordinator.BeginFrame(state);
			watch.OnFrameSubmitted(state.m_FrameSerial);
			coordinator.OnFrameSubmitted();
			auto production = std::make_shared<NonFiniteProductionFrame>();
			production->m_FrameSerial = state.m_FrameSerial;
			production->m_Claim = { NonFiniteClaimStatus::Failed, NonFiniteWatchFailure::NonFiniteReplaced };
			production->m_Sites.push_back({ .m_ProgramIndex = 2, .m_SiteIndex = 3, .m_Claim = production->m_Claim });
			control.m_Production.push_back(std::move(production));
			coordinator.Update();
			const auto early = coordinatorJobs.TakeResults();
			context.Check((mode == NonFiniteWatchMode::Fail ? early.size() == 1 && early[0].m_Products.size() == 3 : early.empty()) &&
				!std::filesystem::exists(directory.GetPath()),
				"Fail mode terminates the entire plan before readbacks arrive; report mode preserves the required products");
			for (const auto& issued : control.m_Issued)
			{
				control.Complete(issued.m_Id);
			}
			coordinator.Update();
			const auto late = coordinatorJobs.TakeResults();
			const auto& results = mode == NonFiniteWatchMode::Fail ? early : late;
			context.Check(results.size() == 1 && results[0].m_Products.size() == 3 && std::ranges::all_of(results[0].m_Products, [&](const auto& result)
				{
					return result.m_Status == (mode == NonFiniteWatchMode::Fail ?
						CaptureJobStatus::Failed :
						CaptureJobStatus::Completed) &&
						result.m_Metadata && result.m_Metadata->m_WatchCoverage &&
						result.m_Metadata->m_WatchCoverage->m_FirstAnomaly.has_value();
				}) && (mode != NonFiniteWatchMode::Fail || late.empty()) &&
				coordinator.GetUnfinishedRequestCount() == 0 && control.GetUnfinishedRequestCount() == 0,
				"Report publishes both outputs with anomaly evidence; fail mode drains "
				"both late products without duplicate results");
			coordinator.FinalizeAfterRenderHost();
		}
	}
}

namespace gglab
{
	using namespace capture_test;

	void RunCaptureBundlesSelfTests(SelfTestContext& context) noexcept
	{
		RunBundleProvenanceTests(context);
		RunCaptureEnvironmentTests(context);
		RunBundlePublicationTests(context);
		RunBundleSingleProductTests(context);
		RunBundleManifestShutdownTests(context);
		RunRequiredPlanLifecycleTests(context);
		RunRequiredPlanRetryTests(context);
		RunRequiredPlanWatchTests(context);
	}
}
