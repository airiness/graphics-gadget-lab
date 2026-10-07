#include "FrameCaptureCoordinatorSelfTests.h"

#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "GGLabRuntime/Graphics/CameraPath.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfileFrameSnapshot.h"
#include "GGLabTestCore/SelfTest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// Headless contracts for the capture coordinator: request timing, settling,
// result publication, file output and shutdown. A fake Runtime control stands in
// for GPU capture; PNG encoding and file writes are real.
namespace gglab
{
	namespace
	{
		class FakeCaptureControl final : public FrameCaptureControlBase
		{
		public:
			struct Issued
			{
				uint64_t m_Id = 0;
				FrameCaptureSource m_Source = FrameCaptureSource::Scene;
				std::optional<PostProcessDebugTap> m_DiagnosticTap;
			};

			uint64_t RequestCapture(FrameCaptureSource source) noexcept override
			{
				m_Issued.push_back({ .m_Id = m_NextId, .m_Source = source });
				return m_NextId++;
			}
			uint64_t RequestDiagnosticCapture(PostProcessDebugTap tap) noexcept override
			{
				m_Issued.push_back({ .m_Id = m_NextId, .m_Source = FrameCaptureSource::Diagnostic,
					.m_DiagnosticTap = tap });
				return m_NextId++;
			}
			void ConsumeResults(std::vector<FrameCaptureResult>& outResults) noexcept override
			{
				outResults.insert(outResults.end(), std::make_move_iterator(m_Results.begin()),
					std::make_move_iterator(m_Results.end()));
				m_Results.clear();
			}
			uint32_t GetUnfinishedRequestCount() const noexcept override { return 0; }

			void Complete(uint64_t id, uint64_t frameSerial) noexcept
			{
				auto image = std::make_shared<FrameCaptureImage>();
				image->m_Format = RHIFormat::B8G8R8A8Unorm;
				image->m_Width = 2;
				image->m_Height = 2;
				image->m_Pixels = { 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 10, 20, 30, 0 };
				m_Results.push_back({
					.m_RequestId = id,
					.m_Status = FrameCaptureStatus::Completed,
					.m_FrameSerial = frameSerial,
					.m_Image = std::move(image),
					});
			}
			void Fail(uint64_t id, std::string failure) noexcept
			{
				m_Results.push_back({
					.m_RequestId = id,
					.m_Status = FrameCaptureStatus::Failed,
					.m_Failure = std::move(failure),
					});
			}

			std::vector<Issued> m_Issued;

		private:
			std::vector<FrameCaptureResult> m_Results;
			uint64_t m_NextId = 100;
		};

		class TemporaryDirectory final
		{
		public:
			explicit TemporaryDirectory(std::string_view name) noexcept
			{
				static std::atomic<uint32_t> sequence = 0;
				std::error_code errorCode;
				m_Path = std::filesystem::temp_directory_path(errorCode) /
					std::format("gglab-{}-{}-{}", name,
						std::chrono::steady_clock::now().time_since_epoch().count(),
						sequence.fetch_add(1));
			}
			TemporaryDirectory(const TemporaryDirectory&) = delete;
			TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
			~TemporaryDirectory() noexcept
			{
				std::error_code errorCode;
				std::filesystem::remove_all(m_Path, errorCode);
			}

			[[nodiscard]] const std::filesystem::path& GetPath() const noexcept { return m_Path; }

		private:
			std::filesystem::path m_Path;
		};

		[[nodiscard]] FrameCaptureFrameState MakeFrameState(
			bool ready, uint64_t temporalSession = 1, std::string labId = "gglab.lab.test")
		{
			FrameCaptureFrameState state{
				.m_Backend = "dx12",
				.m_DemoId = "Demo.LabHost",
				.m_LabId = std::move(labId),
				.m_SettleKey = { .m_TemporalSession = temporalSession, .m_Width = 64,
					.m_Height = 32 },
				.m_FrameIndex = 42,
				.m_Camera = { .m_Name = "Main", .m_Position = { 1.0f, 2.0f, 3.0f },
					.m_VerticalFovDegrees = 60.0f },
				.m_FixedDeltaTime = 1.0 / 60.0,
				.m_TotalTime = 0.5,
			};
			state.m_Readiness.Add("shaders", FrameCaptureGateState::Ready);
			state.m_Readiness.Add("lab",
				ready ? FrameCaptureGateState::Ready : FrameCaptureGateState::Pending,
				ready ? "" : "Warming up.");
			return state;
		}

		[[nodiscard]] std::vector<FrameCaptureRequestResult> Consume(
			FrameCaptureCoordinator& coordinator) noexcept
		{
			std::vector<FrameCaptureRequestResult> results;
			coordinator.ConsumeResults(results);
			return results;
		}

		[[nodiscard]] std::string ReadText(const std::filesystem::path& path) noexcept
		{
			std::ifstream file(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		}

		// Stands in for the host's PNG encoder: a PNG signature followed by the
		// image bytes is enough for the publication contracts tested here.
		[[nodiscard]] std::optional<std::vector<uint8_t>> EncodeTestPng(
			const FrameCaptureImage& image) noexcept
		{
			std::vector<uint8_t> bytes{ 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
			bytes.insert(bytes.end(), image.m_Pixels.begin(), image.m_Pixels.end());
			return bytes;
		}

		[[nodiscard]] bool HasPartialFile(const std::filesystem::path& directory) noexcept
		{
			std::error_code errorCode;
			for (const auto& entry : std::filesystem::directory_iterator(directory, errorCode))
			{
				if (entry.path().extension() == ".partial")
				{
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool HasPngSignature(const std::filesystem::path& path) noexcept
		{
			constexpr std::array<char, 8> signature{ '\x89', 'P', 'N', 'G', '\r', '\n', '\x1A',
				'\n' };
			const std::string bytes = ReadText(path);
			return bytes.size() > signature.size() &&
				std::equal(signature.begin(), signature.end(), bytes.begin());
		}

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

			const uint64_t id = coordinator.Submit({
				.m_Source = FrameCaptureSource::Composited,
				.m_Label = "loading \"probe\"",
				});
			context.Check(id != 0 && control.m_Issued.empty() &&
				coordinator.GetUnfinishedRequestCount() == 1,
				"Requests wait for the next frame before reaching the Runtime");

			coordinator.BeginFrame(MakeFrameState(false));
			context.Check(control.m_Issued.size() == 1 &&
				control.m_Issued[0].m_Source == FrameCaptureSource::Composited,
				"A next-frame capture is issued even while content is still loading");
			coordinator.OnFrameSubmitted();
			if (control.m_Issued.empty())
			{
				return;
			}

			control.Complete(control.m_Issued[0].m_Id, 17);
			coordinator.Update();
			const std::vector<FrameCaptureRequestResult> results = Consume(coordinator);
			const bool completed = results.size() == 1 && results[0].m_RequestId == id &&
				results[0].m_Status == FrameCaptureRequestStatus::Completed &&
				results[0].m_Metadata;
			context.Check(completed && coordinator.GetUnfinishedRequestCount() == 0,
				"A completed Runtime capture finishes once its files are written");
			if (!completed)
			{
				return;
			}

			const FrameCaptureRequestResult& result = results[0];
			const FrameCaptureMetadata& metadata = *result.m_Metadata;
			context.Check(std::filesystem::exists(result.m_ImagePath) &&
				std::filesystem::exists(result.m_MetadataPath) &&
				HasPngSignature(result.m_ImagePath) &&
				result.m_ImagePath.parent_path() == directory.GetPath() &&
				!HasPartialFile(directory.GetPath()),
				"The PNG and its metadata sidecar are published without partial files");
			context.Check(metadata.m_FrameSerial == 17 && metadata.m_Width == 2 &&
				metadata.m_Height == 2 && metadata.m_DisplayFormat == "B8G8R8A8Unorm" &&
				metadata.m_LabId == "gglab.lab.test" && metadata.m_FrameIndex == 42 &&
				metadata.m_Timing == FrameCaptureTiming::NextFrame &&
				!metadata.m_Readiness.IsReady() &&
				metadata.m_ImageFile == result.m_ImagePath.filename().string(),
				"Metadata records the frame, content, image and readiness at issue time");

			const std::string json = ReadText(result.m_MetadataPath);
			context.Check(json.find("\"schemaVersion\": 1") != std::string::npos &&
				json.find(std::format("\"requestId\": {}", id)) != std::string::npos &&
				json.find("\"label\": \"loading \\\"probe\\\"\"") != std::string::npos &&
				json.find("\"source\": \"composited\"") != std::string::npos &&
				json.find("\"ready\": false") != std::string::npos,
				"The sidecar is schema-versioned JSON with escaped strings");
			context.Check(result.m_ImagePath.filename().string().starts_with(
				"loading__probe_-composited-"),
				"File names start with the sanitized label and the capture source");
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
			const uint64_t id = coordinator.Submit({
				.m_Timing = FrameCaptureTiming::AfterReady,
				.m_SettleFrames = 2,
				});

			coordinator.BeginFrame(MakeFrameState(false));
			coordinator.OnFrameSubmitted();
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
			control.Complete(control.m_Issued[0].m_Id, 9);
			coordinator.Update();
			const std::vector<FrameCaptureRequestResult> results = Consume(coordinator);
			context.Check(results.size() == 1 && results[0].m_RequestId == id &&
				results[0].m_Metadata && results[0].m_Metadata->m_SettledFrames == 2 &&
				results[0].m_Metadata->m_SettleFrames == 2 &&
				results[0].m_Metadata->m_Readiness.IsReady(),
				"After-ready metadata records the settled frames that preceded the capture");

			const uint64_t matching = coordinator.Submit({
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

		[[nodiscard]] FrameCaptureFrameState MakeViewFrameState(uint64_t cameraResetSerial)
		{
			FrameCaptureFrameState state = MakeFrameState(true);
			state.m_SettleKey.m_CameraResetSerial = cameraResetSerial;
			state.m_ReferenceViewIds = { "CAM_A", "CAM_B" };
			return state;
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
			const uint64_t plain = coordinator.Submit({});
			const uint64_t viewA = coordinator.Submit({
				.m_Timing = FrameCaptureTiming::AfterReady,
				.m_SettleFrames = 1,
				.m_ReferenceViewId = "CAM_A",
				});
			const uint64_t viewB = coordinator.Submit({ .m_ReferenceViewId = "CAM_B" });

			const bool noViewBeforeFrame = !coordinator.GetPendingViewChange();
			coordinator.BeginFrame(MakeViewFrameState(0));
			coordinator.OnFrameSubmitted();
			const std::optional<FrameCaptureViewChange> firstChange =
				coordinator.GetPendingViewChange();
			context.Check(noViewBeforeFrame && control.m_Issued.size() == 1 && firstChange &&
				firstChange->m_RequestId == viewA && firstChange->m_ReferenceViewId == "CAM_A",
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

			const std::optional<FrameCaptureViewChange> secondChange =
				coordinator.GetPendingViewChange();
			context.Check(secondChange && secondChange->m_RequestId == viewB,
				"The next view applies only after the previous view capture was issued");
			coordinator.OnReferenceViewApplied(viewB, false);
			control.Complete(control.m_Issued[0].m_Id, 1);
			control.Complete(control.m_Issued[1].m_Id, 3);
			coordinator.Update();
			const std::vector<FrameCaptureRequestResult> results = Consume(coordinator);
			const auto find = [&](uint64_t id) -> const FrameCaptureRequestResult*
				{
					const auto result = std::ranges::find(results, id,
						&FrameCaptureRequestResult::m_RequestId);
					return result != results.end() ? &*result : nullptr;
				};
			const FrameCaptureRequestResult* plainResult = find(plain);
			const FrameCaptureRequestResult* viewAResult = find(viewA);
			const FrameCaptureRequestResult* viewBResult = find(viewB);
			context.Check(viewBResult && viewBResult->m_Status == FrameCaptureRequestStatus::Failed &&
				viewBResult->m_Failure.find("'CAM_B'") != std::string::npos &&
				viewBResult->m_Failure.find("available: CAM_A, CAM_B") != std::string::npos,
				"A view the content cannot restore fails with the available view ids");
			context.Check(plainResult && viewAResult && viewAResult->m_Metadata &&
				plainResult->m_Metadata && plainResult->m_Metadata->m_Camera.m_ReferenceViewId.empty() &&
				viewAResult->m_Metadata->m_Camera.m_ReferenceViewId == "CAM_A" &&
				viewAResult->m_ImagePath.filename().string().find("-CAM_A-scene-") !=
				std::string::npos &&
				ReadText(viewAResult->m_MetadataPath).find("\"referenceView\": \"CAM_A\"") !=
				std::string::npos,
				"Metadata and file names record the restored reference view");

			// Another camera cut before the capture replaces the view, so it is restored again.
			const uint64_t viewAgain = coordinator.Submit({
				.m_Timing = FrameCaptureTiming::AfterReady,
				.m_SettleFrames = 2,
				.m_ReferenceViewId = "CAM_B",
				});
			const std::optional<FrameCaptureViewChange> againChange =
				coordinator.GetPendingViewChange();
			coordinator.OnReferenceViewApplied(viewAgain, true);
			coordinator.BeginFrame(MakeViewFrameState(5));
			coordinator.OnFrameSubmitted();
			const bool heldAfterApply = !coordinator.GetPendingViewChange();
			coordinator.BeginFrame(MakeViewFrameState(6));
			coordinator.OnFrameSubmitted();
			const std::optional<FrameCaptureViewChange> reapply = coordinator.GetPendingViewChange();
			context.Check(againChange && heldAfterApply && reapply &&
				reapply->m_RequestId == viewAgain && coordinator.Cancel(viewAgain),
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

			const uint64_t waiting = coordinator.Submit({ .m_Timing = FrameCaptureTiming::AfterReady });
			const uint64_t failing = coordinator.Submit({});
			coordinator.BeginFrame(MakeFrameState(false));
			const bool cancelled = coordinator.Cancel(waiting);
			const bool issuedCancel = !control.m_Issued.empty() &&
				coordinator.Cancel(failing);
			if (!control.m_Issued.empty())
			{
				control.Fail(control.m_Issued[0].m_Id, "Device lost.");
			}
			coordinator.Update();
			const std::vector<FrameCaptureRequestResult> results = Consume(coordinator);
			context.Check(cancelled && !issuedCancel && results.size() == 2 &&
				results[0].m_RequestId == waiting &&
				results[0].m_Status == FrameCaptureRequestStatus::Cancelled &&
				!results[0].m_Metadata && results[1].m_RequestId == failing &&
				results[1].m_Status == FrameCaptureRequestStatus::Failed &&
				results[1].m_Failure == "Device lost." && results[1].m_Metadata,
				"Waiting requests cancel without metadata; Runtime failures keep their reason");

			const uint64_t blocked = coordinator.Submit({ .m_Timing = FrameCaptureTiming::AfterReady });
			const uint64_t otherContent = coordinator.Submit({
				.m_Timing = FrameCaptureTiming::AfterReady,
				.m_RequiredContentId = "gglab.lab.other",
				});
			FrameCaptureFrameState failedState = MakeFrameState(true);
			failedState.m_Readiness.Add("lab", FrameCaptureGateState::Failed, "Session creation failed.");
			coordinator.BeginFrame(failedState);
			const std::vector<FrameCaptureRequestResult> failedGate = Consume(coordinator);
			const bool otherContentWaiting = coordinator.Cancel(otherContent);
			const std::vector<FrameCaptureRequestResult> cancelledOther = Consume(coordinator);
			context.Check(failedGate.size() == 1 && failedGate[0].m_RequestId == blocked &&
				failedGate[0].m_Status == FrameCaptureRequestStatus::Failed &&
				failedGate[0].m_Failure == "Readiness failed: lab (Session creation failed.)." &&
				otherContentWaiting && cancelledOther.size() == 1,
				"A failed gate fails waiting after-ready captures of that content at once");

			TemporaryDirectory directory("frame-capture-no-encoder");
			FrameCaptureCoordinator withoutEncoder({
				.m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(),
				.m_WriteOnCallingThread = true,
				});
			const uint64_t notEncoded = withoutEncoder.Submit({});
			withoutEncoder.BeginFrame(MakeFrameState(true));
			control.Complete(control.m_Issued.back().m_Id, 2);
			withoutEncoder.Update();
			const std::vector<FrameCaptureRequestResult> noEncoder = Consume(withoutEncoder);
			context.Check(noEncoder.size() == 1 && noEncoder[0].m_RequestId == notEncoded &&
				noEncoder[0].m_Status == FrameCaptureRequestStatus::Failed &&
				noEncoder[0].m_Failure == "No capture image encoder is configured." &&
				!std::filesystem::exists(directory.GetPath()),
				"Without a host image encoder captures fail instead of writing files");

			const uint64_t noDirectory = coordinator.Submit({});
			coordinator.BeginFrame(MakeFrameState(true));
			control.Complete(control.m_Issued.back().m_Id, 3);
			coordinator.Update();
			const std::vector<FrameCaptureRequestResult> unwritable = Consume(coordinator);
			context.Check(unwritable.size() == 1 && unwritable[0].m_RequestId == noDirectory &&
				unwritable[0].m_Status == FrameCaptureRequestStatus::Failed &&
				unwritable[0].m_ImagePath.empty(),
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
			const uint64_t waiting = coordinator.Submit({ .m_Timing = FrameCaptureTiming::AfterReady });
			const uint64_t completed = coordinator.Submit({});
			const uint64_t lost = coordinator.Submit({ .m_Source = FrameCaptureSource::Composited });
			coordinator.BeginFrame(MakeFrameState(false));

			coordinator.PrepareForShutdown();
			const uint64_t late = coordinator.Submit({});
			// The render host finalizes: one capture completed, the other lost.
			control.Complete(control.m_Issued[0].m_Id, 5);
			coordinator.FinalizeAfterRenderHost();
			const std::vector<FrameCaptureRequestResult> results = Consume(coordinator);

			const auto find = [&](uint64_t id) -> const FrameCaptureRequestResult*
				{
					for (const FrameCaptureRequestResult& result : results)
					{
						if (result.m_RequestId == id)
						{
							return &result;
						}
					}
					return nullptr;
				};
			const FrameCaptureRequestResult* waitingResult = find(waiting);
			const FrameCaptureRequestResult* completedResult = find(completed);
			const FrameCaptureRequestResult* lostResult = find(lost);
			const FrameCaptureRequestResult* lateResult = find(late);
			context.Check(results.size() == 4 && waitingResult && completedResult && lostResult &&
				lateResult &&
				waitingResult->m_Status == FrameCaptureRequestStatus::Cancelled &&
				completedResult->m_Status == FrameCaptureRequestStatus::Completed &&
				std::filesystem::exists(completedResult->m_ImagePath) &&
				lostResult->m_Status == FrameCaptureRequestStatus::Failed &&
				lateResult->m_Status == FrameCaptureRequestStatus::Cancelled &&
				coordinator.GetUnfinishedRequestCount() == 0,
				"Shutdown gives every request one explicit result and encodes final captures");
		}

		void RunNameCollisionTests(SelfTestContext& context) noexcept
		{
			TemporaryDirectory directory("frame-capture-names");
			std::error_code errorCode;
			std::filesystem::create_directories(directory.GetPath(), errorCode);
			// Another process already published the preferred names: an image for
			// request 1 and only a sidecar for request 2, in any second the
			// captures below may be stamped with.
			std::vector<std::filesystem::path> occupied;
			const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
			for (int32_t offset = -1; offset <= 2; ++offset)
			{
				const std::string time = std::format("{:%Y%m%d-%H%M%S}", now + std::chrono::seconds(offset));
				occupied.push_back(directory.GetPath() / std::format("shared-scene-dx12-{}-r1.png", time));
				occupied.push_back(directory.GetPath() / std::format("sidecar-scene-dx12-{}-r2.json", time));
			}
			for (const std::filesystem::path& path : occupied)
			{
				std::ofstream(path, std::ios::binary) << "occupied";
			}

			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({
				.m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(),
				.m_ImageEncoder = &EncodeTestPng,
				.m_WriteOnCallingThread = true,
				});
			const uint64_t imageTaken = coordinator.Submit({ .m_Label = "shared" });
			const uint64_t sidecarTaken = coordinator.Submit({ .m_Label = "sidecar" });
			coordinator.BeginFrame(MakeFrameState(true));
			control.Complete(control.m_Issued[0].m_Id, 1);
			control.Complete(control.m_Issued[1].m_Id, 1);
			coordinator.Update();
			const std::vector<FrameCaptureRequestResult> results = Consume(coordinator);

			const auto published = [&](uint64_t id, std::string_view suffix)
				{
					const auto result = std::ranges::find(results, id,
						&FrameCaptureRequestResult::m_RequestId);
					if (result == results.end() ||
						result->m_Status != FrameCaptureRequestStatus::Completed ||
						!result->m_Metadata)
					{
						return false;
					}
					const std::string file = result->m_ImagePath.filename().string();
					return file.ends_with(suffix) && result->m_Metadata->m_ImageFile == file &&
						ReadText(result->m_MetadataPath).find(
							std::format("\"file\": \"{}\"", file)) != std::string::npos;
				};
			context.Check(results.size() == 2 && published(imageTaken, "-r1-2.png") &&
				published(sidecarTaken, "-r2-2.png") &&
				std::ranges::all_of(occupied, [](const std::filesystem::path& path)
					{
						return ReadText(path) == "occupied";
					}) &&
				!HasPartialFile(directory.GetPath()),
				"Captures never replace files of another writer and take the next free name");
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
			const uint64_t id = coordinator.Submit({});
			coordinator.BeginFrame(MakeFrameState(true));
			control.Complete(control.m_Issued[0].m_Id, 11);

			std::vector<FrameCaptureRequestResult> results;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (results.empty() && std::chrono::steady_clock::now() < deadline)
			{
				coordinator.Update();
				coordinator.ConsumeResults(results);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			context.Check(results.size() == 1 && results[0].m_RequestId == id &&
				results[0].m_Status == FrameCaptureRequestStatus::Completed &&
				HasPngSignature(results[0].m_ImagePath) && encoderThread->load(),
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

		[[nodiscard]] bool HasAnyFile(const std::filesystem::path& directory) noexcept
		{
			std::error_code errorCode;
			if (!std::filesystem::exists(directory, errorCode))
			{
				return false;
			}
			for (const auto& entry : std::filesystem::directory_iterator(directory, errorCode))
			{
				if (entry.is_regular_file(errorCode))
				{
					return true;
				}
			}
			return false;
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

			std::vector<uint64_t> acceptedIds{ coordinator.Submit({}) };
			coordinator.BeginFrame(MakeFrameState(true));
			control.Complete(control.m_Issued[0].m_Id, 1);
			coordinator.Update();
			{
				std::unique_lock lock(gate->m_Mutex);
				const bool entered = gate->m_Condition.wait_for(lock, std::chrono::seconds(10),
					[&gate]() { return gate->m_Entered; });
				context.Check(entered, "The running writer job is blocked before filling its queue");
			}

			for (uint32_t index = 0; index < 8; ++index)
			{
				acceptedIds.push_back(coordinator.Submit({}));
			}
			const std::array<uint64_t, 2> rejectedIds{
				coordinator.Submit({ .m_OutputDirectory = rejectedDirectory }),
				coordinator.Submit({ .m_OutputDirectory = rejectedDirectory }),
			};
			coordinator.BeginFrame(MakeFrameState(true));
			for (size_t index = 1; index < control.m_Issued.size(); ++index)
			{
				control.Complete(control.m_Issued[index].m_Id, 2);
			}
			coordinator.Update();
			const auto rejected = Consume(coordinator);
			context.Check(rejected.size() == rejectedIds.size() &&
				rejected[0].m_RequestId == rejectedIds[0] && rejected[1].m_RequestId == rejectedIds[1] &&
				std::ranges::all_of(rejected, [](const FrameCaptureRequestResult& result)
					{
						return result.m_Status == FrameCaptureRequestStatus::Failed &&
							result.m_Failure == "Capture writer queue is full." &&
							result.m_ImagePath.empty() && result.m_MetadataPath.empty();
					}),
				"A full writer queue immediately fails each new capture with an explicit result");
			context.Check(coordinator.GetUnfinishedRequestCount() == 9 &&
				!HasAnyFile(directory.GetPath()),
				"The writer accepts one running job and exactly eight pending jobs");

			{
				std::scoped_lock lock(gate->m_Mutex);
				gate->m_Released = true;
			}
			gate->m_Condition.notify_all();
			std::vector<FrameCaptureRequestResult> completed;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (completed.size() < acceptedIds.size() &&
				std::chrono::steady_clock::now() < deadline)
			{
				coordinator.Update();
				coordinator.ConsumeResults(completed);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			context.Check(completed.size() == acceptedIds.size() &&
				std::ranges::all_of(acceptedIds, [&completed](uint64_t id)
					{
						return std::ranges::count(completed, id,
							&FrameCaptureRequestResult::m_RequestId) == 1;
					}) &&
				std::ranges::all_of(completed, [](const FrameCaptureRequestResult& result)
					{
						return result.m_Status == FrameCaptureRequestStatus::Completed &&
							HasPngSignature(result.m_ImagePath) && !ReadText(result.m_MetadataPath).empty();
					}),
				"All accepted writer jobs publish their files and finish exactly once after release");

			const uint64_t resumedId = coordinator.Submit({});
			coordinator.BeginFrame(MakeFrameState(true));
			control.Complete(control.m_Issued.back().m_Id, 3);
			coordinator.PrepareForShutdown();
			coordinator.FinalizeAfterRenderHost();
			const auto resumed = Consume(coordinator);
			context.Check(resumed.size() == 1 && resumed[0].m_RequestId == resumedId &&
				resumed[0].m_Status == FrameCaptureRequestStatus::Completed &&
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
			std::vector<FrameCaptureRequestResult> results;
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
				encodingId = coordinator.Submit({});
				queuedId = coordinator.Submit({ .m_Source = FrameCaptureSource::Composited });
				coordinator.BeginFrame(MakeFrameState(true));
				control.Complete(control.m_Issued[0].m_Id, 1);
				control.Complete(control.m_Issued[1].m_Id, 1);
				coordinator.PrepareForShutdown();
				{
					std::unique_lock lock(gate->m_Mutex);
					gate->m_Condition.wait_for(lock, std::chrono::seconds(10),
						[&gate]() { return gate->m_Entered; });
				}

				const auto finalizeStart = std::chrono::steady_clock::now();
				coordinator.FinalizeAfterRenderHost();
				finalizeTime = std::chrono::steady_clock::now() - finalizeStart;
				coordinator.ConsumeResults(results);
			}
			context.Check(results.size() == 2 &&
				std::ranges::all_of(results, [](const FrameCaptureRequestResult& result)
					{
						return result.m_Status == FrameCaptureRequestStatus::Failed &&
							result.m_Failure.find("abandoned") != std::string::npos &&
							result.m_ImagePath.empty();
					}) &&
				results[0].m_RequestId == encodingId && results[1].m_RequestId == queuedId &&
				results[0].m_Failure.find("stage: encoding") != std::string::npos &&
				results[1].m_Failure.find("stage: queued") != std::string::npos &&
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

		void RunMetadataSerializationTests(SelfTestContext& context) noexcept
		{
			FrameCaptureMetadata metadata{
				.m_RequestId = 7,
				.m_Note = "line\nbreak\ttab\\slash\x01",
				.m_FixedDeltaTime = std::nullopt,
				.m_TotalTime = std::numeric_limits<double>::infinity(),
			};
			metadata.m_Readiness.Add("ibl", FrameCaptureGateState::Pending, "Baking.");
			const std::string json = SerializeFrameCaptureMetadata(metadata);
			context.Check(
				json.find("\"note\": \"line\\nbreak\\ttab\\\\slash\\u0001\"") != std::string::npos &&
				json.find("\"fixedDeltaTime\": null") != std::string::npos &&
				json.find("\"totalTime\": null") != std::string::npos &&
				json.find("\"state\": \"pending\"") != std::string::npos,
				"Metadata JSON escapes control characters and writes non-finite numbers as null");

			metadata.m_Temporal = {
				.m_Requested = true,
				.m_Status = "active",
				.m_DisableReason = "none",
				.m_SessionIdentity = 5,
				.m_ResetIdentity = 9,
				.m_JitterIndex = 3,
				.m_JitterSequenceLength = 8,
				.m_JitterPixels = { -0.375f, -0.0625f },
				.m_MaxHistoryFeedback = 0.97f,
				.m_RenderExtent = { 1280, 720 },
				.m_DisplayExtent = { 1280, 720 },
			};
			metadata.m_Sequence = FrameCaptureSequenceInfo{
				.m_SequenceId = 2,
				.m_CameraPathId = "SEQ_Test",
				.m_CameraPathVersion = 4,
				.m_Frame = 17,
				.m_FrameCount = 96,
			};
			const std::string sequenceJson = SerializeFrameCaptureMetadata(metadata);
			context.Check(
				sequenceJson.find("\"status\": \"active\"") != std::string::npos &&
				sequenceJson.find("\"jitterIndex\": 3") != std::string::npos &&
				sequenceJson.find("\"jitterSequenceLength\": 8") != std::string::npos &&
				sequenceJson.find("-0.375") != std::string::npos &&
				sequenceJson.find("\"maxHistoryFeedback\": 0.97") != std::string::npos &&
				sequenceJson.find("\"cameraPath\": \"SEQ_Test\"") != std::string::npos &&
				sequenceJson.find("\"cameraPathVersion\": 4") != std::string::npos &&
				sequenceJson.find("\"frame\": 17") != std::string::npos &&
				json.find("\"sequence\": null") != std::string::npos,
				"Metadata records temporal state, jitter and settings, and the sequence frame "
				"when the capture belongs to a sequence");
		}
	}

	namespace
	{
		[[nodiscard]] CameraPath MakeSequenceTestPath()
		{
			const auto key = [](uint32_t frame, float x, bool cut = false)
				{
					return CameraPathKey{ .m_Frame = frame, .m_Position = { x, 1.0f, 0.0f },
						.m_Target = { x, 1.0f, 10.0f }, .m_Cut = cut };
				};
			return {
				.m_Id = "SEQ_Test",
				.m_Name = "Test",
				.m_Version = 2,
				.m_Keys = { key(0, 0.0f), key(3, 3.0f), key(4, 40.0f, true), key(5, 41.0f) },
			};
		}

		// Stands in for the runtime frame loop: poses the frame, builds its state,
		// issues captures, submits it and completes every issued Runtime capture.
		struct SequenceHarness
		{
			explicit SequenceHarness(const std::filesystem::path& directory) noexcept :
				m_Capture({
					.m_Capture = &m_Control,
					.m_DefaultOutputDirectory = directory,
					.m_ImageEncoder = &EncodeTestPng,
					.m_WriteOnCallingThread = true,
					}),
				m_Sequence(m_Capture)
			{
				m_Paths.push_back(MakeSequenceTestPath());
			}

			// Renders one frame; returns the posed sequence frame, if any.
			std::optional<uint32_t> Frame(bool submit = true, uint64_t temporalSession = 1)
			{
				m_Deferred = false;
				if (m_Sequence.ShouldDeferFrame())
				{
					// The runtime neither simulates nor renders a deferred frame.
					m_Deferred = true;
					m_Capture.Update();
					m_Sequence.OnCaptureResults(Consume(m_Capture));
					return std::nullopt;
				}
				const std::optional<FrameSequencePoseRequest> pose =
					m_Sequence.PrepareFrame(m_Capture.GetLastFrameState(), m_Paths);
				m_LastSample = pose ? pose->m_ReferenceSample : std::nullopt;
				m_LastTemporalAAOverrides = pose
					? std::optional(pose->m_TemporalAAOverrides)
					: std::nullopt;
				if (pose)
				{
					const std::optional<CameraPathPose> applied =
						EvaluateCameraPath(m_Paths.front(), pose->m_Frame);
					if (applied && applied->m_Cut)
					{
						++m_CameraResetSerial;
					}
					m_Sequence.OnPoseApplied(applied);
				}
				FrameCaptureFrameState state = MakeFrameState(m_Ready, temporalSession);
				state.m_SettleKey.m_CameraResetSerial = m_CameraResetSerial;
				m_Sequence.BeginFrame(state);
				m_Capture.BeginFrame(std::move(state));
				if (submit)
				{
					m_Capture.OnFrameSubmitted();
					m_Sequence.OnFrameSubmitted();
				}
				for (; m_AutoComplete && m_Completed < m_Control.m_Issued.size(); ++m_Completed)
				{
					m_Control.Complete(m_Control.m_Issued[m_Completed].m_Id, 7);
				}
				m_Capture.Update();
				std::vector<FrameCaptureRequestResult> results = Consume(m_Capture);
				m_Sequence.OnCaptureResults(results);
				m_Results.insert(m_Results.end(), results.begin(), results.end());
				return pose ? std::optional<uint32_t>(pose->m_Frame) : std::nullopt;
			}

			FakeCaptureControl m_Control;
			FrameCaptureCoordinator m_Capture;
			FrameSequenceCoordinator m_Sequence;
			std::vector<CameraPath> m_Paths;
			uint64_t m_CameraResetSerial = 1;
			size_t m_Completed = 0;
			bool m_Ready = true;
			bool m_AutoComplete = true;
			bool m_Deferred = false;
			std::optional<TemporalReferenceSample> m_LastSample;
			std::optional<FrameSequenceTemporalAAOverrides> m_LastTemporalAAOverrides;
			std::vector<FrameCaptureRequestResult> m_Results;
		};

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
			for (const auto& entry :
				std::filesystem::directory_iterator(directory.GetPath(), errorCode))
			{
				labelled |= entry.path().filename().string().starts_with("run-f0005-");
			}
			context.Check(labelled, "Sequence captures are labelled with their sequence frame");
			const auto lastCapture = std::ranges::find_if(harness.m_Results,
				[](const FrameCaptureRequestResult& result)
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
			for (uint32_t frame = 0; frame < FrameCaptureCoordinator::MaxPendingWriteJobs; ++frame)
			{
				limited.Frame();
			}
			const size_t issuedAtLimit = limited.m_Control.m_Issued.size();
			const std::optional<uint32_t> deferred = limited.Frame();
			const bool deferredAgain = !limited.Frame() && limited.m_Deferred;
			context.Check(issuedAtLimit == FrameCaptureCoordinator::MaxPendingWriteJobs &&
				!deferred && deferredAgain &&
				limited.m_Control.m_Issued.size() == issuedAtLimit &&
				limited.m_Sequence.GetStatus()->m_SubmittedFrames ==
				FrameCaptureCoordinator::MaxPendingWriteJobs,
				"A capture beyond the writer limit defers the frame without posing or submitting it");

			limited.m_AutoComplete = true;
			limited.m_Control.Complete(limited.m_Control.m_Issued[0].m_Id, 7);
			limited.m_Completed = 1;
			// The next tick is still deferred; it drains the finished capture.
			const std::optional<uint32_t> draining = limited.Frame();
			const std::optional<uint32_t> resumed = limited.Frame();
			context.Check(!draining && resumed == FrameCaptureCoordinator::MaxPendingWriteJobs,
				"The sequence resumes with the deferred frame once the writer drains");
			while (limited.m_Sequence.IsActive())
			{
				limited.Frame();
			}
			const FrameSequenceStatus& status = *limited.m_Sequence.GetStatus();
			context.Check(status.m_State == FrameSequenceState::Completed &&
				status.m_CompletedCaptures == 21 && status.m_SubmittedFrames == 21,
				"Every frame of a fully captured sequence is captured without writer overflow");
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
			const uint64_t missingTap = coordinator.Submit({ .m_Source = FrameCaptureSource::Diagnostic });
			const uint64_t weight = coordinator.Submit({
				.m_Source = FrameCaptureSource::Diagnostic,
				.m_DiagnosticTap = PostProcessDebugTap::TemporalHistoryWeight,
				});
			coordinator.BeginFrame(MakeFrameState(true));
			coordinator.OnFrameSubmitted();
			if (!control.m_Issued.empty())
			{
				control.Complete(control.m_Issued.back().m_Id, 3);
			}
			coordinator.Update();
			const std::vector<FrameCaptureRequestResult> results = Consume(coordinator);
			const auto find = [&](uint64_t id)
				{
					return std::ranges::find(results, id, &FrameCaptureRequestResult::m_RequestId);
				};
			const auto failed = find(missingTap);
			const auto completed = find(weight);
			context.Check(control.m_Issued.size() == 1 &&
				control.m_Issued[0].m_Source == FrameCaptureSource::Diagnostic &&
				control.m_Issued[0].m_DiagnosticTap == PostProcessDebugTap::TemporalHistoryWeight &&
				failed != results.end() && failed->m_Status == FrameCaptureRequestStatus::Failed &&
				completed != results.end() &&
				completed->m_Status == FrameCaptureRequestStatus::Completed &&
				completed->m_Metadata &&
				completed->m_Metadata->m_DiagnosticTap == "temporal-history-weight",
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

		void RunReferenceSequenceTests(SelfTestContext& context) noexcept
		{
			TemporaryDirectory directory("frame-sequence-reference");
			SequenceHarness harness(directory.GetPath());
			std::string error;
			const uint64_t tooMany = harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
				.m_ReferenceSamples = MaxTemporalReferenceSamples + 1 }, error);
			const uint64_t id = harness.m_Sequence.Start({
				.m_CameraPathId = "SEQ_Test",
				.m_CaptureFrames = { 1 },
				.m_ReferenceSamples = 3,
				}, error);
			harness.Frame();

			std::vector<uint32_t> frames;
			std::vector<uint32_t> samples;
			std::vector<bool> heldBefore;
			std::vector<size_t> issued;
			while (harness.m_Sequence.IsActive() && frames.size() < 30)
			{
				heldBefore.push_back(harness.m_Sequence.ShouldHoldTime());
				const std::optional<uint32_t> frame = harness.Frame();
				frames.push_back(frame.value_or(999));
				samples.push_back(harness.m_LastSample ? harness.m_LastSample->m_Index : 999);
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
				[](const FrameCaptureRequestResult& result)
				{
					return result.m_Metadata && result.m_Metadata->m_Sequence;
				});
			context.Check(issued[4] == 0 && issued[5] == 1 && harness.m_Results.size() == 1 &&
				capture != harness.m_Results.end() &&
				capture->m_Metadata->m_Sequence->m_Frame == 1 &&
				capture->m_Metadata->m_Sequence->m_ReferenceSamples == 3,
				"A reference frame is captured once, after its last sample");
		}

		void RunFrameSequenceEvaluationTests(SelfTestContext& context) noexcept
		{
			TemporaryDirectory directory("frame-sequence-evaluation");
			{
				TemporalAASettings content{};
				content.m_Enabled = true;
				content.m_DepthAbsoluteThreshold = 0.25f;
				const TemporalAASettings overridden = ApplyFrameSequenceTemporalAAOverrides({
					.m_MaxHistoryFeedback = 0.9f,
					.m_NeighborhoodClampExpansion = 4.0f,
					.m_HistoryFilter = TemporalAAHistoryFilter::Bilinear,
					.m_CurrentFilter = TemporalAACurrentFilter::Point,
					.m_MotionSelection = TemporalAAMotionSelection::Center,
					}, content);
				TemporalAASettings invalidFilter = content;
				invalidFilter.m_HistoryFilter = static_cast<TemporalAAHistoryFilter>(7);
				invalidFilter.m_CurrentFilter = static_cast<TemporalAACurrentFilter>(7);
				invalidFilter.m_MotionSelection = static_cast<TemporalAAMotionSelection>(7);
				context.Check(overridden.m_Enabled && overridden.m_MaxHistoryFeedback == 0.9f &&
					overridden.m_DepthAbsoluteThreshold == 0.25f &&
					overridden.m_NeighborhoodClampExpansion ==
					TemporalAAMaxNeighborhoodClampExpansion &&
					overridden.m_HistoryFilter == TemporalAAHistoryFilter::Bilinear &&
					content.m_HistoryFilter == TemporalAAHistoryFilter::CatmullRomClamped &&
					ResolveTemporalAASettings(invalidFilter).m_HistoryFilter ==
					TemporalAAHistoryFilter::CatmullRomClamped &&
					overridden.m_CurrentFilter == TemporalAACurrentFilter::Point &&
					content.m_CurrentFilter == TemporalAACurrentFilter::Gaussian &&
					ResolveTemporalAASettings(invalidFilter).m_CurrentFilter ==
					TemporalAACurrentFilter::Gaussian &&
					overridden.m_MotionSelection == TemporalAAMotionSelection::Center &&
					content.m_MotionSelection == TemporalAAMotionSelection::ClosestDepth &&
					ResolveTemporalAASettings(invalidFilter).m_MotionSelection ==
					TemporalAAMotionSelection::ClosestDepth &&
					ApplyFrameSequenceTemporalAAOverrides({}, content) ==
					ResolveTemporalAASettings(content),
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
					.m_TemporalAAOverrides = { .m_MaxHistoryFeedback = 0.9f },
					}, referenceError);
				std::string error;
				const uint64_t id = harness.m_Sequence.Start({
					.m_CameraPathId = "SEQ_Test",
					.m_TemporalAAOverrides = { .m_NeighborhoodClampExpansion = 0.5f },
					}, error);
				harness.Frame();
				bool everyFrameOverridden = true;
				while (harness.m_Sequence.IsActive())
				{
					everyFrameOverridden &= harness.Frame().has_value() &&
						harness.m_LastTemporalAAOverrides &&
						harness.m_LastTemporalAAOverrides->m_NeighborhoodClampExpansion == 0.5f &&
						!harness.m_LastTemporalAAOverrides->m_MaxHistoryFeedback;
				}
				context.Check(reference == 0 && !referenceError.empty() && id != 0 &&
					everyFrameOverridden && !harness.m_Sequence.GetStatus()->m_GpuTiming,
					"Every sequence frame carries the requested Temporal AA overrides; a reference "
					"rejects them");
			}
			{
				SequenceHarness harness(directory.GetPath());
				harness.m_Paths.front().m_Keys.back().m_Frame = 20;
				std::string error;
				GGLAB_UNUSED(harness.m_Sequence.Start(
					{ .m_CameraPathId = "SEQ_Test", .m_GpuTiming = true }, error));
				const bool wantedWhileWaiting = harness.m_Sequence.WantsGpuTiming();
				harness.Frame();
				// Render k of the sequence is profiler frame 100 + k; its profile completes two
				// frames later, so the first two profiles seen while running precede the
				// sequence. Each profile is also reported twice.
				uint64_t profilerFrame = 100;
				while (harness.m_Sequence.IsActive())
				{
					harness.Frame();
					++profilerFrame;
					const uint64_t completed = profilerFrame - 2;
					const GpuProfileFrameSnapshot profile{
						.m_FrameIndex = completed,
						.m_FrameMilliseconds = static_cast<double>(completed),
						.m_Samples = { { .m_Name = "PostProcess.TemporalAA",
							.m_Milliseconds = 0.5, .m_CallCount = 1 } },
					};
					harness.m_Sequence.OnGpuProfile(profile);
					harness.m_Sequence.OnGpuProfile(profile);
				}
				const FrameSequenceStatus& status = *harness.m_Sequence.GetStatus();
				const FrameSequenceGpuTiming* timing =
					status.m_GpuTiming ? &*status.m_GpuTiming : nullptr;
				context.Check(wantedWhileWaiting && !harness.m_Sequence.WantsGpuTiming() &&
					timing && timing->m_FrameMilliseconds.size() == 16 &&
					timing->m_FrameMilliseconds.front() == 103.0 &&
					timing->m_FrameMilliseconds.back() == 118.0 &&
					timing->m_Scopes.size() == 1 &&
					timing->m_Scopes[0].m_Name == "PostProcess.TemporalAA" &&
					timing->m_Scopes[0].m_Milliseconds.size() == 16,
					"Sequence GPU timing records each profiled sequence frame once after warm-up");
			}
		}

		void RunFrameSequenceFailureTests(SelfTestContext& context) noexcept
		{
			TemporaryDirectory directory("frame-sequence-failure");
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
				const uint64_t id =
					harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test" }, error);
				harness.Frame();
				harness.Frame();
				const bool cancelled = harness.m_Sequence.Cancel();
				context.Check(id != 0 && cancelled && !harness.Frame() &&
					harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Cancelled &&
					!harness.m_Sequence.Cancel(),
					"Cancelling stops posing frames; only an active sequence can be cancelled");
			}
		}
	}

	void RunFrameCaptureCoordinatorSelfTests(SelfTestContext& context) noexcept
	{
		RunNextFrameTests(context);
		RunAfterReadyTests(context);
		RunReferenceViewTests(context);
		RunFailureTests(context);
		RunShutdownTests(context);
		RunNameCollisionTests(context);
		RunWriterThreadTests(context);
		RunWriterQueueLimitTests(context);
		RunAbandonedWritingTests(context);
		RunMetadataSerializationTests(context);
		RunFrameSequenceTests(context);
		RunFrameSequenceBackPressureTests(context);
		RunDiagnosticCaptureTests(context);
		RunReferenceSequenceTests(context);
		RunFrameSequenceEvaluationTests(context);
		RunFrameSequenceFailureTests(context);
	}
}
