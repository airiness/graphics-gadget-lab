#include "FrameCaptureCoordinatorSelfTests.h"

#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/ResourceExportWriter.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "Capture/NonFiniteWatchCoordinator.h"
#include "GGLabRuntime/Graphics/CameraPath.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAOTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfileFrameSnapshot.h"
#include "GGLabTestCore/SelfTest.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
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
				uint64_t m_FrameSerial = 0;
				std::optional<PostProcessDebugTap> m_DiagnosticTap;
				std::vector<RGResourceAddress> m_Targets;
				bool m_IncludeGraph = false;
				bool m_Defer = false;
				uint64_t m_GroupId = 0;
			};

			uint64_t RequestCapture(FrameCaptureSource source, uint64_t frameSerial = 0, uint64_t groupId = 0) noexcept override
			{
				if (m_RejectRequest || GetRequestAvailability() != FrameCaptureAvailability::Available)
				{
					return 0;
				}
				m_Issued.push_back({ .m_Id = m_NextId, .m_Source = source, .m_FrameSerial = frameSerial, .m_GroupId = groupId });
				m_LiveRequests.push_back(m_NextId);
				return m_NextId++;
			}
			uint64_t RequestDiagnosticCapture(PostProcessDebugTap tap, uint64_t frameSerial = 0, uint64_t groupId = 0) noexcept override
			{
				if (m_RejectRequest || GetRequestAvailability() != FrameCaptureAvailability::Available)
				{
					return 0;
				}
				m_Issued.push_back({ .m_Id = m_NextId, .m_Source = FrameCaptureSource::Diagnostic,
					.m_FrameSerial = frameSerial,
					.m_DiagnosticTap = tap, .m_GroupId = groupId });
				m_LiveRequests.push_back(m_NextId);
				return m_NextId++;
			}
			uint64_t RequestResourceExport(std::vector<RGResourceAddress> targets, uint64_t frameSerial,
				bool defer, bool includeGraph, uint64_t groupId = 0) noexcept override
			{
				const auto id = RequestCapture(FrameCaptureSource::ResourceExport, frameSerial, groupId);
				if (id != 0)
				{
					m_Issued.back().m_Targets = std::move(targets);
					m_Issued.back().m_Defer = defer;
					m_Issued.back().m_IncludeGraph = includeGraph;
				}
				return id;
			}
			void Busy(uint64_t id) noexcept
			{
				const auto issued = std::ranges::find(m_Issued, id, &Issued::m_Id);
				if (issued != m_Issued.end())
				{
					m_Results.push_back({ .m_RequestId = id, .m_Source = issued->m_Source,
						.m_Status = FrameCaptureStatus::Busy, .m_FrameSerial = issued->m_FrameSerial, .m_ResourceFailure = RGObservationFailure::Busy });
				}
			}
			void ConsumeResults(std::vector<FrameCaptureResult>& outResults) noexcept override
			{
				for (const auto& result : m_Results)
				{
					std::erase(m_LiveRequests, result.m_RequestId);
				}
				outResults.insert(outResults.end(), std::make_move_iterator(m_Results.begin()), std::make_move_iterator(m_Results.end()));
				m_Results.clear();
			}
			bool HasNonFiniteInstrumentation() const noexcept override { return m_Instrumentation; }
			void ConsumeNonFiniteProductionFrames(std::vector<std::shared_ptr<const NonFiniteProductionFrame>>& frames) noexcept override
			{
				frames.insert(frames.end(), std::make_move_iterator(m_Production.begin()), std::make_move_iterator(m_Production.end()));
				m_Production.clear();
			}
			uint32_t GetUnfinishedRequestCount() const noexcept override { return static_cast<uint32_t>(m_LiveRequests.size()); }
			std::optional<PostProcessDebugTap> GetPendingDiagnosticTap() const noexcept override
			{
				return m_PendingDiagnosticTap;
			}
			FrameCaptureAvailability GetResourceExportFrameAvailability() const noexcept override
			{
				return m_ResourceFrameAvailability == FrameCaptureAvailability::Available ? m_RequestAvailability : m_ResourceFrameAvailability;
			}
			FrameCaptureAvailability GetRequestAvailability(uint32_t requestCount = 1) const noexcept override
			{
				m_QueriedRequestCount = requestCount;
				if (m_RequestAvailability != FrameCaptureAvailability::Available)
				{
					return m_RequestAvailability;
				}
				if (requestCount > m_RequestLimit)
				{
					return FrameCaptureAvailability::TooLarge;
				}
				return m_LiveRequests.size() > m_RequestLimit - requestCount ? FrameCaptureAvailability::Busy : FrameCaptureAvailability::Available;
			}
			FrameCaptureAvailability GetAvailability(RHIFormat format, uint32_t width, uint32_t height) const noexcept override
			{
				m_QueriedFormat = format;
				m_QueriedExtent = { width, height };
				return m_Availability;
			}

			void Complete(uint64_t id, std::optional<uint64_t> frameSerial = std::nullopt) noexcept
			{
				const auto issued = std::ranges::find(m_Issued, id, &Issued::m_Id);
				if (issued == m_Issued.end())
				{
					return;
				}
				auto image = std::make_shared<FrameCaptureImage>();
				image->m_Format = RHIFormat::B8G8R8A8Unorm;
				image->m_Width = 2;
				image->m_Height = 2;
				image->m_Pixels = { 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 10, 20, 30, 0 };
				std::shared_ptr<RGSnapshot> graph;
				if (issued->m_Source == FrameCaptureSource::RenderGraph || issued->m_IncludeGraph)
				{
					graph = std::make_shared<RGSnapshot>();
					graph->m_Execution.m_Recorded = true;
				}
				std::shared_ptr<ResourceExportPayload> resources;
				if (issued->m_Source == FrameCaptureSource::ResourceExport)
				{
					resources = std::make_shared<ResourceExportPayload>();
					for (const auto& target : issued->m_Targets)
					{
						ResourceExportArray array{ .m_Address = target, .m_Scalar = ResourceExportScalar::Float32,
							.m_Shape = { 1, 1, 2, 1 } };
						const float values[] = { 1.25f, -2.0f };
						array.m_Data.resize(sizeof(values));
						std::memcpy(array.m_Data.data(), values, sizeof(values));
						resources->m_Arrays.push_back(std::move(array));
					}
				}
				m_Results.push_back({
					.m_RequestId = id,
					.m_Source = issued->m_Source,
					.m_Status = FrameCaptureStatus::Completed,
					.m_FrameSerial = frameSerial.value_or(issued->m_FrameSerial),
					.m_DiagnosticTap = issued->m_DiagnosticTap,
					.m_Image = graph || resources ? nullptr : std::move(image),
					.m_Graph = graph,
					.m_Resources = resources,
					.m_ResourceExportActive = bool(resources),
					.m_SubmissionFenceIndex = 3,
					.m_SubmissionFenceGeneration = 1,
					.m_SubmissionFenceValue = 19,
					.m_AdapterCompatibilityIdentity = "Capture.TestAdapter",
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
			std::optional<PostProcessDebugTap> m_PendingDiagnosticTap;
			FrameCaptureAvailability m_Availability = FrameCaptureAvailability::Available;
			FrameCaptureAvailability m_RequestAvailability = FrameCaptureAvailability::Available;
			FrameCaptureAvailability m_ResourceFrameAvailability = FrameCaptureAvailability::Available;
			bool m_RejectRequest = false;
			bool m_Instrumentation = false;
			std::vector<std::shared_ptr<const NonFiniteProductionFrame>> m_Production;
			uint32_t m_RequestLimit = 64;
			mutable RHIFormat m_QueriedFormat = RHIFormat::Unknown;
			mutable uint32_t m_QueriedRequestCount = 0;
			mutable std::array<uint32_t, 2> m_QueriedExtent{};

		private:
			std::vector<uint64_t> m_LiveRequests;
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
					std::format("gglab-{}-{}-{}", name, std::chrono::steady_clock::now().time_since_epoch().count(), sequence.fetch_add(1));
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
				.m_SettleKey = { .m_Temporal = {
					.m_Identity = { .m_DisplayViewId = RenderViewID::Main,
						.m_SessionIdentity = temporalSession,
						.m_ColorExtent = { 64, 32 }, .m_DepthExtent = { 64, 32 } },
					.m_Services = TemporalService::FrameContinuity | TemporalService::ColorDepthHistory,
					.m_HasCompatiblePreviousHistory = true } },
				.m_DisplayFormat = RHIFormat::B8G8R8A8Unorm,
				.m_FrameSerial = 17,
				.m_FrameIndex = 42,
				.m_Camera = { .m_Name = "Main", .m_Position = { 1.0f, 2.0f, 3.0f },
					.m_VerticalFovDegrees = 60.0f },
				.m_FixedDeltaTime = 1.0 / 60.0,
				.m_TotalTime = 0.5,
			};
			state.m_Readiness.Add("shaders", FrameCaptureGateState::Ready);
			state.m_Readiness.Add("lab", ready ? FrameCaptureGateState::Ready : FrameCaptureGateState::Pending, ready ? "" : "Warming up.");
			return state;
		}

		[[nodiscard]] std::vector<FrameCaptureRequestResult> Consume(FrameCaptureCoordinator& coordinator) noexcept
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
		[[nodiscard]] std::optional<std::vector<uint8_t>> EncodeTestPng(const FrameCaptureImage& image) noexcept
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
			context.Check(metadata.m_SceneInputs && metadata.m_SceneInputs->m_Exposure.m_ManualEV100 == 15.0f,
				"Only the issued frame's built scene inputs reach its capture metadata");
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
			context.Check(result.m_ImagePath.filename().string().starts_with("loading__probe_-composited-"),
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

		void RunHistoryContinuitySettlingTests(SelfTestContext& context) noexcept
		{
			TemporaryDirectory directory("frame-capture-continuity");
			for (uint32_t change = 0; change < 4; ++change)
			{
				FakeCaptureControl control;
				FrameCaptureCoordinator coordinator({ .m_Capture = &control,
					.m_DefaultOutputDirectory = directory.GetPath(),
					.m_ImageEncoder = &EncodeTestPng, .m_WriteOnCallingThread = true });
				FrameCaptureFrameState state = MakeFrameState(true);
				coordinator.BeginFrame(state);
				coordinator.OnFrameSubmitted();
				coordinator.BeginFrame(state);
				coordinator.OnFrameSubmitted();
				GGLAB_UNUSED(coordinator.Submit({ .m_Timing = FrameCaptureTiming::AfterReady,
					.m_SettleFrames = 2 }));
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
				const auto results = Consume(coordinator);
				context.Check(results.size() == 1 && results.front().m_Metadata &&
					results.front().m_Metadata->m_SettledFrames == 2,
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
			const bool idleKeepsTime = !coordinator.ShouldHoldTime() &&
				!coordinator.ShouldRestartTemporalHistory();
			const uint64_t id = coordinator.Submit({
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
			const uint64_t again = coordinator.Submit({
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
			const bool cancelled = coordinator.Cancel(again);
			const uint64_t next = coordinator.Submit({});
			context.Check(cancelled && next != 0 && !coordinator.ShouldHoldTime() &&
				!coordinator.ShouldRestartTemporalHistory(),
				"Only waiting after-ready captures hold time or restart temporal history");
		}

		[[nodiscard]] FrameCaptureFrameState MakeViewFrameState(uint64_t cameraResetSerial)
		{
			FrameCaptureFrameState state = MakeFrameState(true);
			state.m_SettleKey.m_Temporal.m_Identity.m_ResetIdentity = cameraResetSerial;
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
			const std::optional<FrameCaptureViewChange> firstChange = coordinator.GetPendingViewChange();
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

			const std::optional<FrameCaptureViewChange> secondChange = coordinator.GetPendingViewChange();
			context.Check(secondChange && secondChange->m_RequestId == viewB,
				"The next view applies only after the previous view capture was issued");
			coordinator.OnReferenceViewApplied(viewB, false);
			control.Complete(control.m_Issued[0].m_Id);
			control.Complete(control.m_Issued[1].m_Id);
			coordinator.Update();
			const std::vector<FrameCaptureRequestResult> results = Consume(coordinator);
			const auto find = [&](uint64_t id) -> const FrameCaptureRequestResult*
			{
				const auto result = std::ranges::find(results, id, &FrameCaptureRequestResult::m_RequestId);
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
				viewAResult->m_ImagePath.filename().string().find("-CAM_A-scene-") != std::string::npos &&
				ReadText(viewAResult->m_MetadataPath).find("\"referenceView\": \"CAM_A\"") != std::string::npos,
				"Metadata and file names record the restored reference view");

			// Another camera cut before the capture replaces the view, so it is restored again.
			const uint64_t viewAgain = coordinator.Submit({
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
			control.Complete(control.m_Issued.back().m_Id);
			withoutEncoder.Update();
			const std::vector<FrameCaptureRequestResult> noEncoder = Consume(withoutEncoder);
			context.Check(noEncoder.size() == 1 && noEncoder[0].m_RequestId == notEncoded &&
				noEncoder[0].m_Status == FrameCaptureRequestStatus::Failed &&
				noEncoder[0].m_Failure == "No capture image encoder is configured." &&
				!std::filesystem::exists(directory.GetPath()),
				"Without a host image encoder captures fail instead of writing files");

			const uint64_t noDirectory = coordinator.Submit({});
			coordinator.BeginFrame(MakeFrameState(true));
			control.Complete(control.m_Issued.back().m_Id);
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
			control.Complete(control.m_Issued[0].m_Id);
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
					const auto id = coordinator.Submit({});
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
					const auto results = Consume(coordinator);
					const bool publishes = mode == NonFiniteWatchMode::Report || claim == NonFiniteClaimStatus::Passed;
					context.Check(results.size() == 1 && results[0].m_RequestId == id && results[0].m_Metadata &&
						results[0].m_Metadata->m_WatchCoverage && results[0].m_Metadata->m_WatchCoverage->m_Complete &&
						results[0].m_Metadata->m_WatchCoverage->m_CompletedFrames == 1 &&
						results[0].m_Metadata->m_WatchCoverage->m_Production.m_Status == claim &&
						results[0].m_Status == (publishes ? FrameCaptureRequestStatus::Completed : FrameCaptureRequestStatus::Failed) &&
						(publishes ?
							std::filesystem::exists(results[0].m_ImagePath) :
							results[0].m_ImagePath.empty() && results[0].m_Failure.starts_with(
								claim == NonFiniteClaimStatus::Failed ? "NonFiniteReplaced" : "MissingCoverage")) &&
						coordinator.GetUnfinishedRequestCount() == 0,
						"Shutdown drains final production evidence before adjudicating only genuinely missing coverage");
				}
			}
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
			control.Complete(control.m_Issued[0].m_Id);
			control.Complete(control.m_Issued[1].m_Id);
			coordinator.Update();
			const std::vector<FrameCaptureRequestResult> results = Consume(coordinator);

			const auto published = [&](uint64_t id, std::string_view suffix)
			{
				const auto result = std::ranges::find(results, id, &FrameCaptureRequestResult::m_RequestId);
				if (result == results.end() ||
					result->m_Status != FrameCaptureRequestStatus::Completed ||
					!result->m_Metadata)
				{
					return false;
				}
				const std::string file = result->m_ImagePath.filename().string();
				return file.ends_with(suffix) && result->m_Metadata->m_ImageFile == file &&
					ReadText(result->m_MetadataPath).find(std::format("\"file\": \"{}\"", file)) != std::string::npos;
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
			control.Complete(control.m_Issued[0].m_Id);

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
			for (const auto availability : { FrameCaptureAvailability::Busy,
				FrameCaptureAvailability::TooLarge, FrameCaptureAvailability::Unsupported })
			{
				control.m_Availability = availability;
				const uint64_t id = coordinator.Submit({});
				coordinator.BeginFrame(MakeFrameState(true));
				const auto failed = Consume(coordinator);
				context.Check(failed.size() == 1 && failed[0].m_RequestId == id &&
					failed[0].m_Status == FrameCaptureRequestStatus::Failed &&
					!failed[0].m_Failure.empty() && control.m_Issued.empty(),
					"Unavailable payload capacity fails an ordinary capture before GPU work");
			}
			control.m_Availability = FrameCaptureAvailability::Available;
			control.m_RejectRequest = true;
			const uint64_t rejected = coordinator.Submit({});
			coordinator.BeginFrame(MakeFrameState(true));
			const auto noSlot = Consume(coordinator);
			context.Check(noSlot.size() == 1 && noSlot[0].m_RequestId == rejected &&
				noSlot[0].m_Status == FrameCaptureRequestStatus::Failed &&
				coordinator.GetUnfinishedRequestCount() == 0 && control.m_Issued.empty(),
				"A zero Runtime request id fails exactly once without reserving a write slot");
			control.m_RejectRequest = false;
			for (uint32_t index = 0; index < FrameCaptureCoordinator::MaxInFlightCaptures; ++index)
			{
				GGLAB_UNUSED(coordinator.Submit({}));
			}
			coordinator.BeginFrame(MakeFrameState(true));
			const uint64_t excess = coordinator.Submit({});
			coordinator.BeginFrame(MakeFrameState(true));
			const auto overflow = Consume(coordinator);
			context.Check(control.m_Issued.size() == FrameCaptureCoordinator::MaxInFlightCaptures &&
				overflow.size() == 1 && overflow[0].m_RequestId == excess &&
				coordinator.GetCaptureAvailability(FrameCaptureSource::Scene) == FrameCaptureAvailability::Busy,
				"GPU captures still awaiting results reserve every write slot before any encoding begins");
			control.Fail(control.m_Issued.front().m_Id, "Submission failed.");
			coordinator.Update();
			GGLAB_UNUSED(Consume(coordinator));
			const uint64_t cancelled = coordinator.Submit({});
			context.Check(coordinator.Cancel(cancelled) &&
				coordinator.GetCaptureAvailability(FrameCaptureSource::Scene) == FrameCaptureAvailability::Available,
				"Submission failure restores write capacity and cancelling a waiting request consumes no slot");
			GGLAB_UNUSED(Consume(coordinator));
			GGLAB_UNUSED(coordinator.Submit({ .m_Source = FrameCaptureSource::Diagnostic,
				.m_DiagnosticTap = PostProcessDebugTap::TemporalHistoryWeight }));
			coordinator.BeginFrame(MakeFrameState(true));
			context.Check(control.m_QueriedFormat == RHIFormat::R8G8B8A8Unorm &&
				control.m_QueriedExtent == std::array<uint32_t, 2>{ 64, 32 } &&
					control.m_Issued.back().m_Source == FrameCaptureSource::Diagnostic,
				"Diagnostic admission queries the display-sized RGBA8 target independently of presentation format");
			coordinator.PrepareForShutdown();
			coordinator.FinalizeAfterRenderHost();
			const auto shutdown = Consume(coordinator);
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
			const uint64_t viewA = coordinator.Submit({ .m_Timing = FrameCaptureTiming::AfterReady,
				.m_SettleFrames = 1, .m_ReferenceViewId = "CAM_A", .m_WaitForCapacity = true });
			const uint64_t viewB = coordinator.Submit({ .m_ReferenceViewId = "CAM_B", .m_WaitForCapacity = true });
			control.m_Availability = FrameCaptureAvailability::Busy;
			coordinator.BeginFrame(MakeViewFrameState(0));
			coordinator.OnFrameSubmitted();
			context.Check(!coordinator.GetPendingViewChange() && control.m_Issued.empty() &&
				Consume(coordinator).empty() && coordinator.GetUnfinishedRequestCount() == 2,
				"A flexible view batch waits for Runtime capacity before changing its camera");

			control.m_Availability = FrameCaptureAvailability::Available;
			const auto change = coordinator.GetPendingViewChange();
			context.Check(change && change->m_RequestId == viewA,
				"Capacity release resumes the first waiting view in submission order");
			coordinator.OnReferenceViewApplied(viewA, true);
			// Capacity can change between the camera query and the actual frame.
			control.m_Availability = FrameCaptureAvailability::Busy;
			coordinator.BeginFrame(MakeViewFrameState(1));
			coordinator.OnFrameSubmitted();
			coordinator.BeginFrame(MakeViewFrameState(1));
			coordinator.OnFrameSubmitted();
			context.Check(control.m_Issued.empty() && !coordinator.GetPendingViewChange() &&
				Consume(coordinator).empty() && coordinator.GetUnfinishedRequestCount() == 2,
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
			const auto completed = Consume(coordinator);
			context.Check(completed.size() == 1 && completed[0].m_RequestId == viewA &&
				completed[0].m_Status == FrameCaptureRequestStatus::Completed && completed[0].m_Metadata &&
				completed[0].m_Metadata->m_FrameSerial == 42 &&
				completed[0].m_Metadata->m_Camera.m_ReferenceViewId == "CAM_A",
				"Deferred view metadata records its admitted frame and camera exactly once");
			control.m_Availability = FrameCaptureAvailability::Busy;
			context.Check(coordinator.Cancel(viewB) && coordinator.GetUnfinishedRequestCount() == 0,
				"A view waiting for capacity remains cancellable without reserving a slot");
			GGLAB_UNUSED(Consume(coordinator));

			for (const auto availability : { FrameCaptureAvailability::TooLarge,
				FrameCaptureAvailability::Unsupported, FrameCaptureAvailability::Finalized })
			{
				control.m_Availability = availability;
				const uint64_t id = coordinator.Submit({ .m_WaitForCapacity = true });
				coordinator.BeginFrame(state);
				const auto failed = Consume(coordinator);
				context.Check(failed.size() == 1 && failed[0].m_RequestId == id &&
					failed[0].m_Status == FrameCaptureRequestStatus::Failed && !failed[0].m_Metadata &&
					coordinator.GetUnfinishedRequestCount() == 0,
					"Permanent capture errors fail a flexible batch instead of waiting indefinitely");
			}
			control.m_Availability = FrameCaptureAvailability::Busy;
			const uint64_t exact = coordinator.Submit({ .m_WaitForCapacity = true,
				.m_Sequence = FrameCaptureSequenceInfo{} });
			coordinator.BeginFrame(state);
			const auto exactFailure = Consume(coordinator);
			context.Check(exactFailure.size() == 1 && exactFailure[0].m_RequestId == exact &&
				exactFailure[0].m_Status == FrameCaptureRequestStatus::Failed &&
				coordinator.GetUnfinishedRequestCount() == 0,
				"An exact sequence frame ignores flexible capacity waiting");

			control.m_Availability = FrameCaptureAvailability::Available;
			const uint64_t issued = coordinator.Submit({ .m_WaitForCapacity = true });
			coordinator.BeginFrame(state);
			if (control.m_Issued.size() == 2)
			{
				control.Fail(control.m_Issued.back().m_Id, "Submission failed.");
			}
			coordinator.Update();
			const auto submissionFailure = Consume(coordinator);
			++state.m_FrameSerial;
			coordinator.BeginFrame(state);
			context.Check(submissionFailure.size() == 1 && submissionFailure[0].m_RequestId == issued &&
				submissionFailure[0].m_Status == FrameCaptureRequestStatus::Failed &&
				control.m_Issued.size() == 2 && coordinator.GetUnfinishedRequestCount() == 0,
				"An issued flexible capture fails once on submission failure and never substitutes a later frame");

			control.m_Availability = FrameCaptureAvailability::Busy;
			const uint64_t waiting = coordinator.Submit({ .m_WaitForCapacity = true });
			coordinator.BeginFrame(state);
			coordinator.PrepareForShutdown();
			coordinator.FinalizeAfterRenderHost();
			const auto shutdown = Consume(coordinator);
			context.Check(shutdown.size() == 1 && shutdown[0].m_RequestId == waiting &&
				shutdown[0].m_Status == FrameCaptureRequestStatus::Cancelled &&
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

			std::vector<uint64_t> acceptedIds{ coordinator.Submit({}) };
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
				acceptedIds.push_back(coordinator.Submit({}));
			}
			const std::array<uint64_t, 2> rejectedIds
			{
				coordinator.Submit({ .m_OutputDirectory = rejectedDirectory }),
				coordinator.Submit({ .m_OutputDirectory = rejectedDirectory }),
			};
			coordinator.BeginFrame(MakeFrameState(true));
			for (size_t index = 1; index < control.m_Issued.size(); ++index)
			{
				control.Complete(control.m_Issued[index].m_Id);
			}
			coordinator.Update();
			const auto rejected = Consume(coordinator);
			context.Check(rejected.size() == rejectedIds.size() &&
				rejected[0].m_RequestId == rejectedIds[0] && rejected[1].m_RequestId == rejectedIds[1] &&
				std::ranges::all_of(rejected, [](const FrameCaptureRequestResult& result)
					{
						return result.m_Status == FrameCaptureRequestStatus::Failed &&
							result.m_Failure == "Capture capacity is unavailable before GPU submission." &&
							result.m_ImagePath.empty() && result.m_MetadataPath.empty();
					}),
				"Reserved write slots reject excess requests before GPU capture with an explicit result");
			context.Check(coordinator.GetUnfinishedRequestCount() == 9 &&
				control.m_Issued.size() == 9 && !HasAnyFile(directory.GetPath()),
				"Only nine GPU captures are issued for one running writer and eight pending jobs");
			const uint64_t deferredId = coordinator.Submit({ .m_ReferenceViewId = "CAM_A", .m_WaitForCapacity = true });
			coordinator.BeginFrame(MakeFrameState(true));
			context.Check(!coordinator.GetPendingViewChange() && Consume(coordinator).empty() &&
				coordinator.GetUnfinishedRequestCount() == 10 && control.m_Issued.size() == 9,
				"A flexible batch waits without changing its camera while the real writer queue is full");

			{
				std::scoped_lock lock(gate->m_Mutex);
				gate->m_Released = true;
			}
			gate->m_Condition.notify_all();
			std::vector<FrameCaptureRequestResult> completed;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (completed.size() < acceptedIds.size() && std::chrono::steady_clock::now() < deadline)
			{
				coordinator.Update();
				coordinator.ConsumeResults(completed);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			context.Check(completed.size() == acceptedIds.size() &&
				std::ranges::all_of(acceptedIds, [&completed](uint64_t id)
					{
						return std::ranges::count(completed, id, &FrameCaptureRequestResult::m_RequestId) == 1;
					}) &&
				std::ranges::all_of(completed, [](const FrameCaptureRequestResult& result)
					{
						return result.m_Status == FrameCaptureRequestStatus::Completed &&
							HasPngSignature(result.m_ImagePath) && !ReadText(result.m_MetadataPath).empty();
					}),
				"All accepted writer jobs publish their files and finish exactly once after release");

			const auto resumedView = coordinator.GetPendingViewChange();
			context.Check(resumedView && resumedView->m_RequestId == deferredId,
				"Draining the writer queue makes its waiting batch view available again");
			coordinator.OnReferenceViewApplied(deferredId, true);
			coordinator.BeginFrame(MakeViewFrameState(1));
			control.Complete(control.m_Issued.back().m_Id);
			coordinator.PrepareForShutdown();
			coordinator.FinalizeAfterRenderHost();
			const auto resumed = Consume(coordinator);
			context.Check(resumed.size() == 1 && resumed[0].m_RequestId == deferredId &&
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
				.m_Consumers = {
					{
						.m_Name = "temporal-aa",
						.m_Requested = true,
						.m_Status = "active",
						.m_DisableReason = "none",
						.m_Services = { "projection-jitter", "geometry-motion" },
					},
					{
						.m_Name = "reference",
						.m_Status = "disabled",
						.m_DisableReason = "not-requested",
					},
				},
				.m_Services = { "projection-jitter", "geometry-motion" },
				.m_SessionIdentity = 5,
				.m_ResetIdentity = 9,
				.m_JitterIndex = 3,
				.m_JitterSequenceLength = 8,
				.m_JitterPixels = { -0.375f, -0.0625f },
				.m_MaxHistoryFeedback = 0.97f,
				.m_RenderExtent = { 640, 360 },
				.m_DisplayExtent = { 1280, 720 },
				.m_RenderScale = 0.5f,
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
				sequenceJson.find("\"name\": \"temporal-aa\"") != std::string::npos &&
				sequenceJson.find("\"name\": \"reference\"") != std::string::npos &&
				sequenceJson.find("\"disableReason\": \"not-requested\"") != std::string::npos &&
				sequenceJson.find("\"geometry-motion\"") != std::string::npos &&
				sequenceJson.find("\"renderScale\": 0.5") != std::string::npos &&
				sequenceJson.find("\"cameraPath\": \"SEQ_Test\"") != std::string::npos &&
				sequenceJson.find("\"cameraPathVersion\": 4") != std::string::npos &&
				sequenceJson.find("\"frame\": 17") != std::string::npos &&
				json.find("\"sequence\": null") != std::string::npos,
				"Metadata records temporal consumers, services, jitter, settings and render "
				"scale, and the sequence frame when the capture belongs to a sequence");
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
				const std::optional<FrameSequencePoseRequest> pose = m_Sequence.PrepareFrame(m_Capture.GetLastFrameState(), m_Paths);
				m_LastSample = pose ? pose->m_ReferenceSample : std::nullopt;
				m_LastTemporalAAOverrides = pose ?
					std::optional(pose->m_TemporalAAOverrides) :
					std::nullopt;
				m_LastGTAOOverrides = pose ? std::optional(pose->m_GTAOOverrides) : std::nullopt;
				if (pose)
				{
					const std::optional<CameraPathPose> applied = EvaluateCameraPath(m_Paths.front(), pose->m_Frame);
					if (applied && applied->m_Cut)
					{
						++m_CameraResetSerial;
					}
					m_Sequence.OnPoseApplied(applied);
				}
				FrameCaptureFrameState state = MakeFrameState(m_Ready, temporalSession);
				state.m_FrameSerial = ++m_RenderSerial;
				state.m_SettleKey.m_Temporal.m_Identity.m_ResetIdentity = m_CameraResetSerial;
				state.m_SettleKey.m_Temporal.m_Identity.m_DepthExtent = m_RenderExtent;
				state.m_SettleKey.m_Temporal.m_Identity.m_Accumulation = m_Accumulation;
				state.m_SettleKey.m_Temporal.m_HasCompatiblePreviousHistory = m_HistoryCompatible;
				if (m_LastSample)
				{
					state.m_SettleKey.m_Temporal.m_Services = TemporalService::ProjectionJitter;
					state.m_SettleKey.m_Temporal.m_ReferenceActive = true;
				}
				m_Sequence.BeginFrame(state);
				m_Capture.BeginFrame(std::move(state));
				if (submit)
				{
					m_Capture.OnFrameSubmitted();
					m_Sequence.OnFrameSubmitted(m_RenderSerial);
				}
				for (; m_AutoComplete && m_Completed < m_Control.m_Issued.size(); ++m_Completed)
				{
					m_Control.Complete(m_Control.m_Issued[m_Completed].m_Id);
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
			uint64_t m_RenderSerial = 0;
			ViewExtent m_RenderExtent{ 64, 32 };
			TemporalAAHistoryAccumulation m_Accumulation = TemporalAAHistoryAccumulation::EffectiveSamples;
			bool m_HistoryCompatible = true;
			size_t m_Completed = 0;
			bool m_Ready = true;
			bool m_AutoComplete = true;
			bool m_Deferred = false;
			std::optional<TemporalReferenceSample> m_LastSample;
			std::optional<FrameSequenceTemporalAAOverrides> m_LastTemporalAAOverrides;
			std::optional<FrameSequenceGTAOOverrides> m_LastGTAOOverrides;
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
			for (const auto& entry : std::filesystem::directory_iterator(directory.GetPath(), errorCode))
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
				control.Complete(control.m_Issued.back().m_Id);
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

		void RunDiagnosticSchedulingTests(SelfTestContext& context) noexcept
		{
			TemporaryDirectory directory("frame-capture-binding");
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({
				.m_Capture = &control, .m_DefaultOutputDirectory = directory.GetPath(),
				.m_ImageEncoder = &EncodeTestPng, .m_WriteOnCallingThread = true,
			});
			const auto weight = PostProcessDebugTap::TemporalHistoryWeight;
			const auto rejection = PostProcessDebugTap::TemporalRejection;
			const uint64_t first = coordinator.Submit({ .m_Source = FrameCaptureSource::Diagnostic,
				.m_DiagnosticTap = weight });
			const uint64_t second = coordinator.Submit({ .m_Source = FrameCaptureSource::Diagnostic,
				.m_DiagnosticTap = rejection });
			GGLAB_UNUSED(coordinator.Submit({ .m_Source = FrameCaptureSource::Diagnostic,
				.m_DiagnosticTap = weight }));
			GGLAB_UNUSED(coordinator.Submit({ .m_Source = FrameCaptureSource::Scene }));
			GGLAB_UNUSED(coordinator.Submit({ .m_Source = FrameCaptureSource::Composited }));
			const uint64_t cancel = coordinator.Submit({ .m_Source = FrameCaptureSource::Diagnostic,
				.m_DiagnosticTap = PostProcessDebugTap::TemporalClipDistance });
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
			context.Check(coordinator.Cancel(cancel), "A conflicting tap stays cancellable before issue");
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
			const auto results = Consume(coordinator);
			const auto a = std::ranges::find(results, first, &FrameCaptureRequestResult::m_RequestId);
			const auto b = std::ranges::find(results, second, &FrameCaptureRequestResult::m_RequestId);
			context.Check(a != results.end() && b != results.end() && a->m_Metadata && b->m_Metadata &&
				a->m_Status == FrameCaptureRequestStatus::Completed &&
				b->m_Status == FrameCaptureRequestStatus::Completed &&
				a->m_Metadata->m_FrameSerial == 100 && a->m_Metadata->m_FrameIndex == 42 &&
				a->m_Metadata->m_TotalTime == 0.5 && a->m_Metadata->m_Camera.m_Position[0] == 1.0f &&
				b->m_Metadata->m_FrameSerial == 101 && b->m_Metadata->m_FrameIndex == 43 &&
				b->m_Metadata->m_TotalTime == 0.75 && b->m_Metadata->m_Camera.m_Position[0] == 9.0f &&
				b->m_Metadata->m_Temporal.m_JitterIndex == 5,
				"Delayed GPU completions keep frame metadata immutable; a deferred tap uses its actual issue frame");

			SequenceHarness harness(directory.GetPath());
			std::string failure;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
				.m_CaptureFrames = { 0 }, .m_CaptureSource = FrameCaptureSource::Diagnostic,
				.m_DiagnosticTap = rejection }, failure));
			harness.Frame();
			GGLAB_UNUSED(harness.m_Capture.Submit({ .m_Source = FrameCaptureSource::Diagnostic,
				.m_DiagnosticTap = weight }));
			harness.Frame();
			context.Check(harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
				harness.m_Control.m_Issued.size() == 1 &&
				harness.m_Control.m_Issued.front().m_DiagnosticTap == weight,
				"A conflicting sequence capture fails on its required frame and never issues a later substitute");
		}

		void RunAttemptRetryTests(SelfTestContext& context) noexcept
		{
			for (const auto source : { FrameCaptureSource::Scene, FrameCaptureSource::Diagnostic,
				FrameCaptureSource::RenderGraph, FrameCaptureSource::ResourceExport })
			{
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
				const uint64_t id = coordinator.Submit({ .m_Source = source,
					.m_Label = "retry", .m_Note = "Keep the logical request across attempts.",
					.m_GraphSummary = true, .m_ResourceTargets = { { .m_Resource = "A" } },
					.m_IncludeGraph = true,
					.m_DiagnosticTap = PostProcessDebugTap::TemporalHistoryWeight });
				auto state = MakeFrameState(true);
				for (uint32_t retry = 0; retry < 2; ++retry)
				{
					coordinator.BeginFrame(state);
					coordinator.SetFrameSceneInputs(state.m_FrameSerial,
						{ .m_LocalLightCount = retry + 1 });
					control.Busy(control.m_Issued.back().m_Id);
					coordinator.Update();
					context.Check(Consume(coordinator).empty() &&
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
				context.Check(control.m_Issued.size() == 3 && Consume(coordinator).empty() &&
					coordinator.GetUnfinishedRequestCount() == 1,
					"A stale Runtime result cannot complete a newer attempt of the same job");

				control.Complete(control.m_Issued.back().m_Id);
				coordinator.Update();
				context.Check(Consume(coordinator).empty() &&
					!std::filesystem::exists(directory.GetPath()),
					"The retried product still waits for its own submitted-frame coverage");
				auto production = std::make_shared<NonFiniteProductionFrame>();
				production->m_FrameSerial = state.m_FrameSerial;
				production->m_Claim = { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None };
				control.m_Production.push_back(std::move(production));
				coordinator.Update();
				const auto completed = Consume(coordinator);
				context.Check(completed.size() == 1 && completed[0].m_RequestId == id &&
					completed[0].m_Status == FrameCaptureRequestStatus::Completed &&
					completed[0].m_Metadata && completed[0].m_Metadata->m_Source == source &&
					completed[0].m_Metadata->m_FrameSerial == state.m_FrameSerial &&
					completed[0].m_Metadata->m_FrameIndex == state.m_FrameIndex &&
					completed[0].m_Metadata->m_TotalTime == state.m_TotalTime &&
					completed[0].m_Metadata->m_Camera.m_Name == state.m_Camera.m_Name &&
					completed[0].m_Metadata->m_SceneInputs &&
					completed[0].m_Metadata->m_SceneInputs->m_LocalLightCount == 99 &&
					completed[0].m_Metadata->m_WatchCoverage &&
					completed[0].m_Metadata->m_WatchCoverage->m_Complete &&
					completed[0].m_Metadata->m_WatchCoverage->m_FrameCount == 1 &&
					completed[0].m_Metadata->m_WatchCoverage->m_FirstFrameSerial == state.m_FrameSerial &&
					completed[0].m_Metadata->m_WatchCoverage->m_LastFrameSerial == state.m_FrameSerial,
					"One logical result uses only the final attempt's provenance and coverage");
				control.Complete(control.m_Issued.back().m_Id);
				coordinator.Update();
				context.Check(Consume(coordinator).empty() &&
					coordinator.GetUnfinishedRequestCount() == 0,
					"A duplicate late result cannot publish or complete the job twice");
			}

			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control });
			const auto id = coordinator.Submit({});
			coordinator.BeginFrame(MakeFrameState(true));
			control.Busy(control.m_Issued.back().m_Id);
			coordinator.Update();
			context.Check(coordinator.Cancel(id), "A job returned from Busy remains cancellable");
			const auto cancelled = Consume(coordinator);
			context.Check(cancelled.size() == 1 && cancelled[0].m_RequestId == id &&
				cancelled[0].m_Status == FrameCaptureRequestStatus::Cancelled &&
				!cancelled[0].m_Metadata && coordinator.GetUnfinishedRequestCount() == 0,
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
				GGLAB_UNUSED(coordinator.Submit({ .m_Source = FrameCaptureSource::Diagnostic,
					.m_DiagnosticTap = PostProcessDebugTap::TemporalHistoryWeight }));
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
				const auto results = Consume(coordinator);
				context.Check(results.size() == 1 && results[0].m_Status == FrameCaptureRequestStatus::Failed &&
					results[0].m_Metadata && results[0].m_Metadata->m_FrameSerial == 17 &&
					results[0].m_ImagePath.empty() && !std::filesystem::exists(directory.GetPath()),
					std::format("A result with mismatched identity component {} never publishes a PNG", mismatch));
			}
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control });
			control.m_PendingDiagnosticTap = PostProcessDebugTap::TemporalHistoryWeight;
			const uint64_t waiting = coordinator.Submit({ .m_Source = FrameCaptureSource::Diagnostic,
				.m_DiagnosticTap = PostProcessDebugTap::TemporalRejection });
			coordinator.BeginFrame(MakeFrameState(true));
			context.Check(control.m_Issued.empty() && coordinator.Cancel(waiting),
				"Scheduling respects a diagnostic tap already queued by another control client");
			GGLAB_UNUSED(coordinator.Submit({}));
			auto noFrame = MakeFrameState(true);
			noFrame.m_FrameSerial = 0;
			coordinator.BeginFrame(noFrame);
			const auto failed = Consume(coordinator);
			context.Check(control.m_Issued.empty() && std::ranges::any_of(failed, [](const auto& result)
				{
					return result.m_Status == FrameCaptureRequestStatus::Failed;
				}), "The coordinator cannot accidentally issue a flexible request without a render serial");
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
				[](const FrameCaptureRequestResult& result)
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
				TemporalAASettings content{};
				content.m_Enabled = true;
				content.m_DepthAbsoluteThreshold = 0.25f;
				const TemporalAASettings overridden = ApplyFrameSequenceTemporalAAOverrides({
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
					ApplyFrameSequenceTemporalAAOverrides({}, content) == ResolveTemporalAASettings(content),
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
				std::string overrideError;
				const uint64_t referenceWithOverrides = harness.m_Sequence.Start({
					.m_CameraPathId = "SEQ_Test",
					.m_ReferenceSamples = 2,
					.m_GTAOOverrides = { .m_TemporalAccumulation = true },
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
					harness.m_LastGTAOOverrides && harness.m_LastGTAOOverrides->IsEmpty(),
					"A reference averages GTAO visibility for the gtao-final-ao tap, rejects other "
					"taps and takes no GTAO overrides");
			}
			{
				SequenceHarness harness(directory.GetPath());
				std::string error;
				const uint64_t id = harness.m_Sequence.Start({
					.m_CameraPathId = "SEQ_Test",
					.m_GTAOOverrides = { .m_TemporalAccumulation = true, .m_TemporalMaxSamples = 8u },
					}, error);
				harness.Frame();
				bool everyFrameOverridden = true;
				while (harness.m_Sequence.IsActive())
				{
					everyFrameOverridden &= harness.Frame().has_value() &&
						harness.m_LastGTAOOverrides &&
						harness.m_LastGTAOOverrides->m_TemporalAccumulation == true &&
						harness.m_LastGTAOOverrides->m_TemporalMaxSamples == 8u;
				}
				const GTAOSettings overridden = ApplyFrameSequenceGTAOOverrides(
					{ .m_TemporalAccumulation = true, .m_TemporalMaxSamples = 999u }, GTAOSettings{});
				context.Check(id != 0 && everyFrameOverridden && overridden.m_TemporalAccumulation &&
					overridden.m_TemporalMaxSamples == GTAOMaxTemporalSamples &&
					ApplyFrameSequenceGTAOOverrides({}, GTAOSettings{}).m_TemporalMaxSamples == GTAOSettings{}.m_TemporalMaxSamples,
					"Every sequence frame carries the requested GTAO overrides, resolved to the "
					"settings' ranges");
			}
		}

		GpuProfileFrameSnapshot MakeSequenceGpuProfile(uint64_t renderSerial) noexcept
		{
			return { .m_FrameIndex = renderSerial + 1000, .m_RenderFrameSerial = renderSerial,
				.m_FrameMilliseconds = static_cast<double>(renderSerial),
				.m_Samples = { { "PostProcess.TemporalAA", 0.5, 1 } } };
		}

		void RunFrameSequenceGpuTimingTests(SelfTestContext& context) noexcept
		{
			TemporaryDirectory directory("frame-sequence-gpu-timing");
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
				context.Check(harness.m_Sequence.Cancel() && !harness.m_Sequence.WantsGpuTiming(),
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
					harness.m_Sequence.OnCaptureResults(Consume(harness.m_Capture));
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

		void RunResourceWritingTests(SelfTestContext& context) noexcept
		{
			TemporaryDirectory directory("resource-writing");
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true });
			const auto id = coordinator.Submit({ .m_Source = FrameCaptureSource::ResourceExport, .m_Label = "resource",
				.m_ResourceTargets = { { .m_Resource = "A" },{ .m_Resource = "B" } }, .m_IncludeGraph = true });
			coordinator.BeginFrame(MakeFrameState(true));
			control.Busy(control.m_Issued.back().m_Id);
			coordinator.Update();
			context.Check(Consume(coordinator).empty(), "A busy group retries without publishing a partial result");
			auto state = MakeFrameState(true);
			state.m_FrameSerial = 21;
			coordinator.BeginFrame(state);
			coordinator.OnFrameSubmitted();
			control.Complete(control.m_Issued.back().m_Id);
			coordinator.Update();
			const auto results = Consume(coordinator);
			context.Check(results.size() == 1 && results[0].m_RequestId == id && results[0].m_Status == FrameCaptureRequestStatus::Completed &&
				results[0].m_ResourcePaths.size() == 2 && !results[0].m_GraphPath.empty() && results[0].m_ImagePath.empty(),
				"The bounded writer atomically publishes every array, optional graph and one group sidecar");
			if (!results.empty() && results[0].m_Status == FrameCaptureRequestStatus::Completed)
			{
				const auto metadata = nlohmann::json::parse(ReadText(results[0].m_MetadataPath));
				const std::string npy = ReadText(results[0].m_ResourcePaths[0]);
				context.Check(metadata["schema"] == "gglab.resource-export" && metadata["frameSerial"] == 21 &&
					metadata["frame"]["frame"]["serial"] == 21 && metadata["targets"].size() == 2 && metadata["submission"]["completed"] == true &&
					metadata["graph"] == GetFrameCapturePathText(results[0].m_GraphPath.filename()),
					"Retried groups refresh frame metadata and retain the shared fence identity");
				const uint16_t headerSize = static_cast<uint8_t>(npy[8]) | (static_cast<uint16_t>(static_cast<uint8_t>(npy[9])) << 8);
				float values[2] = {};
				std::memcpy(values, npy.data() + 10 + headerSize, sizeof(values));
				context.Check(npy.substr(1, 5) == "NUMPY" && npy[6] == 1 && npy[7] == 0 && (headerSize + 10) % 16 == 0 &&
					npy.find("'<f4'") != std::string::npos && npy.find("(1, 1, 2, 1,") != std::string::npos && values[0] == 1.25f && values[1] == -2,
					"NPY v1 headers declare the exact dtype/shape and preserve numerical bytes");
			}
			std::filesystem::create_directories(directory.GetPath());
			const auto occupied = directory.GetPath() / "resource.22.resource-export.1.npy";
			{
				std::ofstream file(occupied, std::ios::binary);
				file << "other publisher";
			}
			GGLAB_UNUSED(coordinator.Submit({ .m_Source = FrameCaptureSource::ResourceExport, .m_Label = "resource",
				.m_ResourceTargets = { { .m_Resource = "A" },{ .m_Resource = "B" } } }));
			state.m_FrameSerial = 22;
			coordinator.BeginFrame(state);
			coordinator.OnFrameSubmitted();
			control.Complete(control.m_Issued.back().m_Id);
			coordinator.Update();
			const auto collision = Consume(coordinator);
			context.Check(collision.size() == 1 && collision[0].m_Status == FrameCaptureRequestStatus::Completed &&
				collision[0].m_MetadataPath.filename() == "resource.22.resource-export-2.json" &&
				ReadText(occupied) == "other publisher" && !std::filesystem::exists(directory.GetPath() / "resource.22.resource-export.0.npy"),
				"A mid-group filename collision withdraws only owned files and republishes the complete group under one new stem");
			// Activity metadata discloses observers, not a successful watch claim.
			FrameCaptureMetadata observedMetadata;
			observedMetadata.m_NonFiniteScanActive = true;
			RGSnapshot observedGraph;
			observedGraph.m_Execution.m_Recorded = true;
			observedGraph.m_ObservationNodes.emplace_back(0, RGObservationTarget{
				.m_Address = { .m_Resource = "Observed" }, .m_Kind = RGObservationKind::NonFiniteScan });
			const auto graphJson = nlohmann::json::parse(SerializeFrameGraphExport(observedGraph, observedMetadata, {}, false, false));
			context.Check(graphJson["schemaVersion"] == 3 && graphJson["observation"]["nonFiniteWatch"] == true &&
				graphJson["observation"]["resourceExport"] == false && graphJson["execution"]["observationNodes"][0]["kind"] == "NonFiniteScan",
				"Scan-only graphs use v3 and distinguish scan activity from resource-copy observation");
			const auto resourceJson = nlohmann::json::parse(SerializeResourceExportMetadata({}, observedMetadata, {}, {}, {}));
			context.Check(resourceJson["schemaVersion"] == 2 && resourceJson["observation"]["nonFiniteWatch"] == true,
				"Resource sidecars with concurrent scans use v2 without asserting production coverage");
			ResourceExportArray structured{ .m_Type = RGResourceType::RGBuffer, .m_Shape = { 2 }, .m_Stride = 16,
				.m_Fields = { { "first", 0, ResourceExportScalar::Uint32, 1 },{ "vector", 8, ResourceExportScalar::Float32, 2 } }, .m_Data = std::vector<uint8_t>(32) };
			const auto header = MakeResourceExportNpyHeader(structured);
			const std::string text(header.begin(), header.end());
			context.Check(text.find("('', '|V4')") != std::string::npos && text.find("'vector', '<f4', (2,)") != std::string::npos,
				"Structured GPU dtypes preserve field offsets, padding, vector counts and stride");
			structured.m_Data.pop_back();
			bool rejected = false;
			try { GGLAB_UNUSED(MakeResourceExportNpyHeader(structured)); }
			catch (const std::exception&)
			{
				rejected = true;
			}
			context.Check(rejected, "Mismatched array shape/stride/data fails before file publication");
			coordinator.FinalizeAfterRenderHost();
		}

		void RunSequenceGroupAdmissionTests(SelfTestContext& context) noexcept
		{
			TemporaryDirectory directory("sequence-group-admission");
			{
				FakeCaptureControl control;
				FrameCaptureCoordinator coordinator({ .m_Capture = &control, .m_DefaultOutputDirectory = directory.GetPath(),
					.m_ImageEncoder = &EncodeTestPng, .m_WriteOnCallingThread = true });
				for (uint32_t index = 0; index < 8; ++index)
				{
					GGLAB_UNUSED(coordinator.Submit({ .m_Source = FrameCaptureSource::RenderGraph, .m_WaitForCapacity = true }));
				}
				const FrameCaptureSequenceInfo sequence{ .m_SequenceId = 77, .m_Frame = 4 };
				const auto image = coordinator.Submit({ .m_Sequence = sequence });
				const auto resources = coordinator.Submit({ .m_Source = FrameCaptureSource::ResourceExport,
					.m_ResourceTargets = { { .m_Resource = "A" } }, .m_IncludeGraph = true, .m_Sequence = sequence });
				auto state = MakeFrameState(true);
				state.m_FrameSerial = 100;
				coordinator.BeginFrame(state);
				context.Check(control.m_Issued.size() == 9 && control.m_Issued[0].m_Source == FrameCaptureSource::Scene &&
					control.m_Issued[1].m_Source == FrameCaptureSource::ResourceExport && control.m_Issued[0].m_GroupId != 0 &&
					control.m_Issued[0].m_GroupId == control.m_Issued[1].m_GroupId && control.m_Issued[1].m_IncludeGraph &&
					Consume(coordinator).empty(),
					"PNG and resource output claim both writer slots before eight earlier ordinary graphs can block the group");
				for (const auto& issued : control.m_Issued)
				{
					control.Complete(issued.m_Id);
				}
				coordinator.OnFrameSubmitted();
				coordinator.Update();
				const auto completed = Consume(coordinator);
				const auto png = std::ranges::find(completed, image, &FrameCaptureRequestResult::m_RequestId);
				const auto arrays = std::ranges::find(completed, resources, &FrameCaptureRequestResult::m_RequestId);
				context.Check(completed.size() == 9 && png != completed.end() && arrays != completed.end() &&
					png->m_Status == FrameCaptureRequestStatus::Completed && arrays->m_Status == FrameCaptureRequestStatus::Completed &&
					png->m_Metadata && arrays->m_Metadata && png->m_Metadata->m_FrameSerial == 100 && arrays->m_Metadata->m_FrameSerial == 100 &&
					png->m_Metadata->m_Sequence && arrays->m_Metadata->m_Sequence &&
					png->m_Metadata->m_Sequence->m_Frame == 4 && arrays->m_Metadata->m_Sequence->m_Frame == 4,
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
				const auto remaining = Consume(coordinator);
				context.Check(remaining.size() == 1 && remaining[0].m_Metadata && remaining[0].m_Metadata->m_FrameSerial == 101 &&
					coordinator.GetUnfinishedRequestCount() == 0,
					"Every request in the eight-graph/PNG/export burst completes without exceeding writer capacity");
			}
			{
				FakeCaptureControl control;
				FrameCaptureCoordinator coordinator({ .m_Capture = &control, .m_DefaultOutputDirectory = directory.GetPath(),
					.m_ImageEncoder = &EncodeTestPng, .m_WriteOnCallingThread = true });
				const FrameCaptureSequenceInfo sequence{ .m_SequenceId = 79, .m_Frame = 5 };
				GGLAB_UNUSED(coordinator.Submit({ .m_Sequence = sequence }));
				GGLAB_UNUSED(coordinator.Submit({ .m_Source = FrameCaptureSource::ResourceExport,
					.m_ResourceTargets = { { .m_Resource = "A" } }, .m_Sequence = sequence }));
				auto state = MakeFrameState(true);
				coordinator.BeginFrame(state);
				const uint64_t groupId = control.m_Issued.empty() ? 0 : control.m_Issued[0].m_GroupId;
				for (const auto& issued : control.m_Issued)
				{
					control.Busy(issued.m_Id);
				}
				coordinator.Update();
				context.Check(Consume(coordinator).empty() && coordinator.GetUnfinishedRequestCount() == 2,
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
				harness.m_AutoComplete = false;
				GGLAB_UNUSED(harness.Frame());
				const uint32_t occupied = requestPressure ? 1u : 8u;
				if (requestPressure)
				{
					harness.m_Control.m_RequestLimit = 2;
				}
				for (uint32_t index = 0; index < occupied; ++index)
				{
					GGLAB_UNUSED(harness.m_Capture.Submit({ .m_Source = FrameCaptureSource::RenderGraph }));
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
				GGLAB_UNUSED(Consume(harness.m_Capture));
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
				const FrameCaptureSequenceInfo sequence{ .m_SequenceId = 88 };
				const auto image = coordinator.Submit({ .m_Sequence = sequence });
				GGLAB_UNUSED(coordinator.Submit({ .m_Source = FrameCaptureSource::ResourceExport, .m_Sequence = sequence }));
				control.m_ResourceFrameAvailability = FrameCaptureAvailability::Busy;
				coordinator.BeginFrame(MakeFrameState(true));
				context.Check(control.m_Issued.empty() && coordinator.Cancel(image),
					"A busy sequence group remains wholly unissued and can be cancelled");
				const auto cancelled = Consume(coordinator);
				context.Check(cancelled.size() == 2 && coordinator.GetUnfinishedRequestCount() == 0 &&
					std::ranges::all_of(cancelled, [](const auto& result) { return result.m_Status == FrameCaptureRequestStatus::Cancelled; }),
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
			const auto image = std::ranges::find_if(harness.m_Results, [](const auto& result) { return !result.m_ImagePath.empty(); });
			const auto resources = std::ranges::find_if(harness.m_Results, [](const auto& result) { return !result.m_ResourcePaths.empty(); });
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

		void RunGraphWritingTests(SelfTestContext& context) noexcept
		{
			TemporaryDirectory directory("graph-writing");
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true });
			const auto id = coordinator.Submit({ .m_Source = FrameCaptureSource::RenderGraph,
				.m_Label = "graph", .m_GraphSummary = false });
			const auto summaryId = coordinator.Submit({ .m_Source = FrameCaptureSource::RenderGraph,
				.m_Label = "graph", .m_GraphSummary = true });
			coordinator.BeginFrame(MakeFrameState(true));
			coordinator.OnFrameSubmitted();
			for (const auto& issued : control.m_Issued)
			{
				control.Complete(issued.m_Id);
			}
			coordinator.Update();
			std::vector<FrameCaptureRequestResult> results;
			coordinator.ConsumeResults(results);
			context.Check(results.size() == 2 && results[0].m_RequestId == id && results[1].m_RequestId == summaryId &&
				results[0].m_Status == FrameCaptureRequestStatus::Completed &&
				results[1].m_Status == FrameCaptureRequestStatus::Completed && results[0].m_ImagePath.empty(),
				"Full and summary graph requests reuse the bounded writer without requiring a PNG encoder");
			if (results.size() == 2 && results[0].m_Status == FrameCaptureRequestStatus::Completed &&
				results[1].m_Status == FrameCaptureRequestStatus::Completed)
			{
				const auto full = nlohmann::json::parse(ReadText(results[0].m_MetadataPath));
				const auto summary = nlohmann::json::parse(ReadText(results[1].m_MetadataPath));
				context.Check(full["schema"] == "gglab.render-graph" && full["frameSerial"] == 17 &&
					full["frame"]["frame"]["serial"] == 17 && full["submission"]["fenceValue"] == 19 &&
					full["submission"]["completed"] == true && full["observation"]["graph"] == true &&
					full["frame"]["adapterDriverIdentity"] == "Capture.TestAdapter" &&
					summary["mode"] == "summary" && !summary.contains("execution"),
					"Graph JSON embeds the same frame block and completed submission identity in both modes");
			}
			coordinator.FinalizeAfterRenderHost();
		}

		void RunGraphFailureTests(SelfTestContext& context) noexcept
		{
			TemporaryDirectory directory("graph-failures");
			{
				FakeCaptureControl control;
				control.m_RequestAvailability = FrameCaptureAvailability::Busy;
				FrameCaptureCoordinator coordinator({ .m_Capture = &control,
					.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true });
				GGLAB_UNUSED(coordinator.Submit({ .m_Source = FrameCaptureSource::RenderGraph }));
				coordinator.BeginFrame(MakeFrameState(true));
				const auto results = Consume(coordinator);
				context.Check(coordinator.GetCaptureAvailability(FrameCaptureSource::RenderGraph) ==
					FrameCaptureAvailability::Busy && control.m_Issued.empty() && results.size() == 1 &&
					results[0].m_Status == FrameCaptureRequestStatus::Failed,
					"Graph coordination respects Runtime request-only admission before issuing");
				coordinator.FinalizeAfterRenderHost();
			}
			{
				FakeCaptureControl control;
				FrameCaptureCoordinator coordinator({ .m_Capture = &control,
					.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true });
				GGLAB_UNUSED(coordinator.Submit({ .m_Source = FrameCaptureSource::RenderGraph,
					.m_Note = std::string(1, '\xFF') }));
				coordinator.BeginFrame(MakeFrameState(true));
				control.Complete(control.m_Issued[0].m_Id);
				coordinator.Update();
				const auto results = Consume(coordinator);
				std::error_code error;
				context.Check(results.size() == 1 && results[0].m_Status == FrameCaptureRequestStatus::Failed &&
					results[0].m_Failure.find("Serializing the graph export failed") != std::string::npos &&
					!std::filesystem::exists(directory.GetPath(), error),
					"Invalid UTF-8 graph metadata fails safely before writing or publishing files");
				coordinator.FinalizeAfterRenderHost();
			}
			std::error_code error;
			std::filesystem::create_directories(directory.GetPath(), error);
			const auto blocker = directory.GetPath() / std::filesystem::path(u8"\u4e2d\u6587 \U0001F9EA blocker");
			{
				std::ofstream file(blocker, std::ios::binary);
				file << "not a directory";
			}
			for (const auto source : { FrameCaptureSource::Scene, FrameCaptureSource::RenderGraph })
			{
				FakeCaptureControl control;
				FrameCaptureCoordinator coordinator({ .m_Capture = &control,
					.m_DefaultOutputDirectory = blocker / "output", .m_ImageEncoder = &EncodeTestPng,
					.m_WriteOnCallingThread = true });
				GGLAB_UNUSED(coordinator.Submit({ .m_Source = source }));
				coordinator.BeginFrame(MakeFrameState(true));
				control.Complete(control.m_Issued[0].m_Id);
				coordinator.Update();
				const auto results = Consume(coordinator);
				context.Check(results.size() == 1 && results[0].m_Status == FrameCaptureRequestStatus::Failed &&
					results[0].m_Failure.find(GetFrameCapturePathText(blocker)) != std::string::npos &&
					results[0].m_MetadataPath.empty() && results[0].m_ImagePath.empty(),
					"Image and graph write failures report Unicode paths as UTF-8 without code-page exceptions");
				coordinator.FinalizeAfterRenderHost();
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
				const uint64_t id = harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test" }, error);
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
		RunResourceWritingTests(context);
		RunResourceSequenceTests(context);
		RunSequenceGroupAdmissionTests(context);
		RunGraphWritingTests(context);
		RunGraphFailureTests(context);
		RunNextFrameTests(context);
		RunAfterReadyTests(context);
		RunHistoryContinuitySettlingTests(context);
		RunLoadingHistoryTests(context);
		RunReferenceViewTests(context);
		RunFailureTests(context);
		RunShutdownTests(context);
		RunWatchShutdownTests(context);
		RunNameCollisionTests(context);
		RunWriterThreadTests(context);
		RunCapacityAdmissionTests(context);
		RunDeferredCapacityTests(context);
		RunWriterQueueLimitTests(context);
		RunAbandonedWritingTests(context);
		RunMetadataSerializationTests(context);
		RunFrameSequenceTests(context);
		RunFrameSequenceBackPressureTests(context);
		RunDiagnosticCaptureTests(context);
		RunDiagnosticSchedulingTests(context);
		RunFrameResultIdentityTests(context);
		RunAttemptRetryTests(context);
		RunReferenceSequenceTests(context);
		RunFrameSequenceEvaluationTests(context);
		RunFrameSequenceGpuTimingTests(context);
		RunFrameSequenceContinuityTests(context);
		RunFrameSequenceFailureTests(context);
	}
}
