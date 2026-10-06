#include "FrameCaptureCoordinatorSelfTests.h"

#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureMetadata.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
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
			};

			uint64_t RequestCapture(FrameCaptureSource source) noexcept override
			{
				m_Issued.push_back({ .m_Id = m_NextId, .m_Source = source });
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
	}
}
