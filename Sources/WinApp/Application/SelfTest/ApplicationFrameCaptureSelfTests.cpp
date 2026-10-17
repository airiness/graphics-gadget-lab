#include "Application/SelfTest/ApplicationFrameCaptureSelfTests.h"
#include "Application/Capture/ApplicationFrameCapture.h"
#include "Application/Platform/Windows/Win32FrameCaptureImageEncoder.h"
#include "Application/Platform/Windows/Win32Shell.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiStateStore.h"
#include "DevTools/DevelopGui/Panels/FrameCapturePanel.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/Capture/ResourceExportTypes.h"

#include <nlohmann/json.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		// Exercises the real coordinator/writer and interactive front end without
		// GPU work. The injected Shell callback never launches a desktop window.
		class FakeCaptureControl final : public FrameCaptureControlBase
		{
		public:
			struct ExportRequest
			{
				uint64_t m_Id = 0;
				std::vector<RGResourceAddress> m_Targets;
				bool m_IncludeGraph = false;
			};

			uint64_t RequestCapture(FrameCaptureSource source, uint64_t frameSerial, uint64_t = 0) noexcept override
			{
				m_Issued.push_back({ .m_RequestId = m_NextId++, .m_Source = source,
					.m_Status = FrameCaptureStatus::Completed, .m_FrameSerial = frameSerial });
				return m_Issued.back().m_RequestId;
			}
			uint64_t RequestDiagnosticCapture(PostProcessDebugTap, uint64_t, uint64_t = 0) noexcept override { return 0; }
			uint64_t RequestResourceExport(std::vector<RGResourceAddress> targets, uint64_t frameSerial,
				bool, bool includeGraph, uint64_t = 0) noexcept override
			{
				const uint64_t id = RequestCapture(FrameCaptureSource::ResourceExport, frameSerial);
				m_Exports.push_back({ id, std::move(targets), includeGraph });
				return id;
			}
			std::optional<PostProcessDebugTap> GetPendingDiagnosticTap() const noexcept override { return std::nullopt; }
			FrameCaptureAvailability GetRequestAvailability(uint32_t = 1) const noexcept override
			{
				return FrameCaptureAvailability::Available;
			}
			FrameCaptureAvailability GetAvailability(RHIFormat, uint32_t, uint32_t) const noexcept override
			{
				return m_Issued.size() >= m_MaxPendingCaptures ? FrameCaptureAvailability::Busy : FrameCaptureAvailability::Available;
			}
			uint32_t GetUnfinishedRequestCount() const noexcept override { return 0; }
			void ConsumeResults(std::vector<FrameCaptureResult>& results) noexcept override
			{
				if (m_HoldResults)
				{
					return;
				}
				for (FrameCaptureResult result : m_Issued)
				{
					if (result.m_Source == FrameCaptureSource::ResourceExport)
					{
						if (m_FailResourceExport)
						{
							result.m_Status = FrameCaptureStatus::Failed;
							result.m_ResourceFailure = RGObservationFailure::UnknownResource;
							result.m_Failure = "The resource is missing from the captured frame.";
						}
						else
						{
							const auto request = std::ranges::find(m_Exports, result.m_RequestId, &ExportRequest::m_Id);
							auto payload = std::make_shared<ResourceExportPayload>();
							for (const auto& target : request->m_Targets)
							{
								payload->m_Arrays.push_back({ .m_Address = target, .m_Scalar = ResourceExportScalar::Uint8,
									.m_Shape = { 1, 1, 1, 1 }, .m_Data = { 42 } });
							}
							result.m_Resources = std::move(payload);
							result.m_ResourceExportActive = true;
							result.m_SubmissionFenceValue = 23;
							if (request->m_IncludeGraph)
							{
								auto graph = std::make_shared<RGSnapshot>();
								graph->m_Passes.push_back({ .m_Observation = true, .m_Name = "Observation.Copy" });
								graph->m_Execution.m_Recorded = true;
								result.m_Graph = std::move(graph);
								result.m_GraphExportActive = true;
							}
						}
					}
					else if (result.m_Source == FrameCaptureSource::RenderGraph)
					{
						auto graph = std::make_shared<RGSnapshot>();
						graph->m_Execution.m_Recorded = true;
						result.m_Graph = std::move(graph);
						result.m_SubmissionFenceValue = 23;
					}
					else
					{
						auto image = std::make_shared<FrameCaptureImage>();
						image->m_Format = RHIFormat::B8G8R8A8Unorm;
						image->m_Width = image->m_Height = 1;
						image->m_Pixels = { 0, 0, 255, 255 };
						result.m_Image = std::move(image);
					}
					results.push_back(std::move(result));
				}
				m_Issued.clear();
				m_Exports.clear();
			}

			std::vector<FrameCaptureResult> m_Issued;
			std::vector<ExportRequest> m_Exports;
			bool m_FailResourceExport = false;
			uint32_t m_MaxPendingCaptures = 8;
			bool m_HoldResults = false;

		private:
			uint64_t m_NextId = 100;
		};

		class CaptureDiagnosticsView final : public DiagnosticsView
		{
		public:
			CaptureDiagnosticsView()
			{
				m_Snapshot.m_Resources.push_back({ .m_Name = "Test.UI.Texture", .m_TextureMipLevels = 2,
					.m_TextureArraySize = 4, .m_ResourceExportSupport = RGObservationFailure::None });
				m_Snapshot.m_Resources.push_back({ .m_Name = "Preview.IBL.EnvironmentCubemap",
					.m_Imported = true, .m_ResourceExportSupport = RGObservationFailure::UnsupportedResource });
				m_Snapshot.m_Passes.push_back({ .m_Occurrence = 1, .m_Name = "Test.UI.Writer", .m_Culled = true,
					.m_Accesses = { { .m_WrittenResourceNodeIndex = 1, .m_ResourceName = "Test.UI.Texture",
						.m_DependencyAccess = RGDependencyAccess::Write } } });
				m_Snapshot.m_Passes.push_back({ .m_Occurrence = 2, .m_Name = "Test.UI.Writer",
					.m_Accesses = { { .m_WrittenResourceNodeIndex = 2, .m_ResourceName = "Test.UI.Texture",
						.m_DependencyAccess = RGDependencyAccess::Write } } });
			}
			void AddResources(uint32_t count)
			{
				for (uint32_t i = 0; i < count; ++i)
				{
					m_Snapshot.m_Resources.push_back({ .m_Name = std::format("Test.UI.Extra.{}", i),
						.m_ResourceExportSupport = RGObservationFailure::None });
				}
			}
			std::vector<SnapshotProfile> GetProfiles() const override { return {}; }

		private:
			const void* GetSnapshotData(SnapshotId id) noexcept override
			{
				return id == SnapshotIdOf<RGSnapshot> ? &m_Snapshot : nullptr;
			}
			RGSnapshot m_Snapshot;
		};

		FrameCaptureFrameState MakeFrameState(uint64_t serial, uint64_t reset = 1) noexcept
		{
			return {
				.m_Backend = "dx12",
				.m_DemoId = "Demo.LabHost",
				.m_LabId = "gglab.lab.test",
				.m_SettleKey = { .m_Temporal = {
					.m_Identity = { .m_DisplayViewId = RenderViewID::Main,
						.m_ResetIdentity = reset, .m_SessionIdentity = 1,
						.m_ColorExtent = { 1, 1 }, .m_DepthExtent = { 1, 1 } },
					.m_Services = TemporalService::FrameContinuity | TemporalService::ColorDepthHistory,
					.m_HasCompatiblePreviousHistory = true } },
				.m_DisplayFormat = RHIFormat::B8G8R8A8Unorm,
				.m_FrameSerial = serial,
				.m_Camera = { .m_Name = "Main" },
				.m_ReferenceViewIds = { "CAM_Test" },
			};
		}

		nlohmann::json ReadJson(const std::filesystem::path& path) noexcept
		{
			std::ifstream file(path, std::ios::binary);
			return nlohmann::json::parse(file, nullptr, false);
		}

		void RunReferenceViewBatchTests(SelfTestContext& context, const std::filesystem::path& directory,
			FrameCaptureSource source) noexcept
		{
			FakeCaptureControl control;
			control.m_HoldResults = true;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory,
				.m_ImageEncoder = &win32::EncodeFrameCapturePng, .m_WriteOnCallingThread = true });
			ApplicationFrameCapture frontEnd({ .m_Coordinator = &coordinator,
				.m_DefaultOutputDirectory = directory,
				.m_ConsumeResults = [&coordinator](std::vector<FrameCaptureRequestResult>& results)
				{
					coordinator.ConsumeResults(results);
				} });
			frontEnd.GetSettings().m_Source = source;
			auto state = MakeFrameState(1);
			state.m_ReferenceViewIds.clear();
			constexpr uint32_t ViewCount = 14;
			for (uint32_t index = 0; index < ViewCount; ++index)
			{
				state.m_ReferenceViewIds.push_back(std::format("CAM_Batch{}", index));
			}
			coordinator.BeginFrame(state);
			coordinator.OnFrameSubmitted();
			context.Check(frontEnd.CaptureReferenceViews() == ViewCount &&
				frontEnd.GetUnfinishedRequestCount() == ViewCount,
				"Capture All Views submits fourteen views with the interactive screenshot settings");
			std::vector<uint64_t> issuedFrames;
			bool viewsInOrder = true;
			const auto captureView = [&](uint32_t index)
				{
					const auto change = coordinator.GetPendingViewChange();
					if (!change)
					{
						viewsInOrder = false;
						return;
					}
					viewsInOrder = viewsInOrder && change->m_ReferenceViewId == state.m_ReferenceViewIds[index];
					coordinator.OnReferenceViewApplied(change->m_RequestId, true);
					++state.m_FrameSerial;
					++state.m_SettleKey.m_Temporal.m_Identity.m_ResetIdentity;
					state.m_Camera.m_Position = { static_cast<float>(index), 0.0f, 0.0f };
					issuedFrames.push_back(state.m_FrameSerial);
					coordinator.BeginFrame(state);
					coordinator.OnFrameSubmitted();
				};
			for (uint32_t index = 0; index < control.m_MaxPendingCaptures; ++index)
			{
				captureView(index);
			}
			context.Check(viewsInOrder && control.m_Issued.size() == 8 &&
				!coordinator.GetPendingViewChange() && frontEnd.GetUnfinishedRequestCount() == ViewCount,
				"The batch reaches eight retained images and keeps the remaining views waiting without a camera cut");
			for (uint32_t index = 0; index < 2; ++index)
			{
				++state.m_FrameSerial;
				coordinator.BeginFrame(state);
				coordinator.OnFrameSubmitted();
				coordinator.Update();
			}
			context.Check(frontEnd.Update().empty() && control.m_Issued.size() == 8 &&
				!coordinator.GetPendingViewChange() && frontEnd.GetUnfinishedRequestCount() == ViewCount,
				"Repeated Busy frames produce no failed batch results or excess Runtime requests");

			control.m_HoldResults = false;
			coordinator.Update();
			std::vector<FrameCaptureRequestResult> completed = frontEnd.Update();
			for (uint32_t index = 8; index < ViewCount; ++index)
			{
				captureView(index);
				coordinator.Update();
				const auto results = frontEnd.Update();
				completed.insert(completed.end(), results.begin(), results.end());
			}
			context.Check(viewsInOrder && completed.size() == ViewCount && issuedFrames.size() == ViewCount &&
				frontEnd.GetUnfinishedRequestCount() == 0 && control.m_Issued.empty(),
				"All fourteen Scene or Composited views finish after capacity drains");
			bool matchingFiles = completed.size() == ViewCount && issuedFrames.size() == ViewCount;
			for (size_t index = 0; matchingFiles && index < completed.size(); ++index)
			{
				const auto& result = completed[index];
				matchingFiles = result.m_Status == FrameCaptureRequestStatus::Completed && result.m_Metadata &&
					result.m_RequestId == index + 1 && result.m_Metadata->m_Source == source &&
					result.m_Metadata->m_FrameSerial == issuedFrames[index] &&
					result.m_Metadata->m_Camera.m_ReferenceViewId == state.m_ReferenceViewIds[index] &&
					result.m_Metadata->m_Camera.m_Position[0] == static_cast<float>(index) &&
					std::filesystem::exists(result.m_ImagePath) && std::filesystem::exists(result.m_MetadataPath);
				if (matchingFiles)
				{
					const auto json = ReadJson(result.m_MetadataPath);
					matchingFiles = !json.is_discarded() && json["frame"]["serial"] == issuedFrames[index] &&
						json["camera"]["referenceView"] == state.m_ReferenceViewIds[index];
				}
			}
			context.Check(matchingFiles,
				"Each batch view publishes one PNG and sidecar with its own admitted frame and camera");
			coordinator.FinalizeAfterRenderHost();
		}
	}

	void RunApplicationFrameCaptureSelfTests(SelfTestContext& context) noexcept
	{
		std::error_code error;
		const std::filesystem::path directory = std::filesystem::temp_directory_path(error) /
			std::format("gglab-interactive-capture-{}", std::chrono::steady_clock::now().time_since_epoch().count()) /
			std::filesystem::path(u8"\u4e2d\u6587 captures");
		std::filesystem::create_directories(directory, error);
		context.Check(!error, "Interactive capture tests create a Unicode output directory with spaces");
		if (error)
		{
			return;
		}

		{
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory,
				.m_ImageEncoder = &win32::EncodeFrameCapturePng, .m_WriteOnCallingThread = true });
			uint32_t openCount = 0;
			bool rejectOpen = false;
			std::filesystem::path openedDirectory;
			ApplicationFrameCapture frontEnd({
				.m_Coordinator = &coordinator,
				.m_DefaultOutputDirectory = directory,
				.m_ConsumeResults = [&coordinator](std::vector<FrameCaptureRequestResult>& results)
				{
					coordinator.ConsumeResults(results);
				},
				.m_OpenDirectory = [&](const std::filesystem::path& path, std::string& failure)
				{
					++openCount;
					openedDirectory = path;
					if (rejectOpen)
					{
						failure = "Injected Shell rejection.";
					}
					return !rejectOpen;
				},
				});
			context.Check(frontEnd.GetSettings().m_Timing == FrameCaptureTiming::NextFrame,
				"Interactive exports default to the next rendered frame without a control session");
			coordinator.BeginFrame(MakeFrameState(16));
			coordinator.OnFrameSubmitted();
			auto& settings = frontEnd.GetSettings();
			settings.m_Source = FrameCaptureSource::Composited;
			settings.m_SettleFrames = 7;
			settings.m_Label = "interactive";
			settings.m_Note = "shared settings";
			settings.m_ReferenceViewId = "CAM_Test";
			const uint64_t fullId = frontEnd.ExportGraph();
			const auto view = coordinator.GetPendingViewChange();
			context.Check(view && view->m_RequestId == fullId && view->m_ReferenceViewId == "CAM_Test",
				"The graph button uses the existing queued reference-view path");
			coordinator.OnReferenceViewApplied(fullId, true);
			settings.m_ReferenceViewId.clear();
			const uint64_t summaryId = frontEnd.ExportGraph(true);
			const uint64_t imageId = frontEnd.Capture();
			std::string failure;
			context.Check(!frontEnd.OpenOutputFolder(fullId, failure) && !failure.empty() && openCount == 0,
				"An unfinished export cannot launch the Shell");
			coordinator.BeginFrame(MakeFrameState(17));
			context.Check(control.m_Issued.size() == 3 &&
				control.m_Issued[0].m_Source == FrameCaptureSource::RenderGraph &&
				control.m_Issued[1].m_Source == FrameCaptureSource::RenderGraph &&
				control.m_Issued[2].m_Source == FrameCaptureSource::Composited &&
				settings.m_Source == FrameCaptureSource::Composited,
				"Both graph buttons leave Capture/F9's selected screenshot source unchanged");
			coordinator.OnFrameSubmitted();
			coordinator.Update();
			const auto results = frontEnd.Update();
			const auto* full = frontEnd.FindResult(fullId);
			const auto* summary = frontEnd.FindResult(summaryId);
			const auto* image = frontEnd.FindResult(imageId);
			context.Check(results.size() == 3 && full && summary && image &&
				full->m_Status == FrameCaptureRequestStatus::Completed &&
				summary->m_Status == FrameCaptureRequestStatus::Completed &&
				image->m_Status == FrameCaptureRequestStatus::Completed,
				"Interactive full, summary and screenshot requests publish through the existing writer");
			if (full && summary && image && full->m_Status == FrameCaptureRequestStatus::Completed &&
				summary->m_Status == FrameCaptureRequestStatus::Completed &&
				image->m_Status == FrameCaptureRequestStatus::Completed)
			{
				const auto fullJson = ReadJson(full->m_MetadataPath);
				const auto summaryJson = ReadJson(summary->m_MetadataPath);
				context.Check(!fullJson.is_discarded() && !summaryJson.is_discarded() &&
					fullJson["mode"] == "full" && fullJson.contains("execution") &&
					summaryJson["mode"] == "summary" && !summaryJson.contains("execution") &&
					full->m_Metadata->m_Label == "interactive" && full->m_Metadata->m_Note == "shared settings" &&
					full->m_Metadata->m_SettleFrames == 0 && full->m_Metadata->m_FrameSerial == 17 &&
					full->m_Metadata->m_Camera.m_ReferenceViewId == "CAM_Test",
					"The interactive path preserves graph mode, shared output settings and next-frame metadata");
				context.Check(ApplicationFrameCapture::GetOutputPath(*full) == full->m_MetadataPath &&
					ApplicationFrameCapture::GetOutputPath(*image) == image->m_ImagePath &&
					std::filesystem::path(full->m_MetadataPath.u8string()) == full->m_MetadataPath &&
					ApplicationFrameCapture::GetPathText(full->m_MetadataPath).find("\xE4\xB8\xAD\xE6\x96\x87") != std::string::npos,
					"Graph notifications select JSON, screenshots select PNG, and path text preserves Unicode");
				context.Check(frontEnd.Update().empty() && openCount == 0 &&
					frontEnd.CanOpenOutputFolder(*full) && frontEnd.CanOpenOutputFolder(*image),
					"Completion is consumed once and never automatically opens a folder");
				// Draw the actual panel without a native window to check its first-use layout.
				ImGuiContext* previousContext = ImGui::GetCurrentContext();
				ImGuiContext* gui = ImGui::CreateContext();
				ImGuiIO& io = ImGui::GetIO();
				io.IniFilename = nullptr;
				io.DisplaySize = ImVec2(1280.0f, 900.0f);
				unsigned char* pixels = nullptr;
				int width = 0;
				int height = 0;
				io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
				DevelopGuiStateStore store;
				DevelopGuiContext drawContext{ .m_StateStore = &store };
				FrameCapturePanel panel(&frontEnd);
				// SetWindowSize during Draw takes effect at the following Begin.
				for (uint32_t frame = 0; frame < 2; ++frame)
				{
					ImGui::NewFrame();
					ImGui::Begin("Interactive capture test");
					panel.Draw(drawContext);
					if (frame == 1)
					{
						context.Check(ImGui::GetWindowWidth() >= 600.0f && ImGui::GetWindowHeight() >= 500.0f,
							"The first-use capture panel has usable space for its controls and history");
					}
					ImGui::End();
					DrawFrameCaptureNotification(frontEnd);
					ImGui::Render();
				}
				ImGui::DestroyContext(gui);
				ImGui::SetCurrentContext(previousContext);
				context.Check(openCount == 0, "Drawing graph/image history and notifications never invokes the Shell");

				settings.m_OutputDirectory = directory / "different setting";
				context.Check(frontEnd.OpenOutputFolder(fullId, failure) && failure.empty() && openedDirectory == directory,
					"Graph folder opening uses the published path rather than the edited output setting");
				rejectOpen = true;
				context.Check(!frontEnd.OpenOutputFolder(imageId, failure) && failure == "Injected Shell rejection." &&
					openedDirectory == directory && image->m_Status == FrameCaptureRequestStatus::Completed,
					"Shell failure is reported separately and leaves the successful screenshot unchanged");
				rejectOpen = false;
				context.Check(frontEnd.OpenOutputFolder(imageId, failure) && failure.empty(),
					"A later successful folder action clears the previous error");

				ApplicationFrameCapture unavailable({ .m_Coordinator = &coordinator,
					.m_ConsumeResults = [result = *full, pending = true](std::vector<FrameCaptureRequestResult>& output) mutable
						{
							if (std::exchange(pending, false))
							{
								output.push_back(result);
							}
						} });
				unavailable.Update();
				context.Check(!unavailable.CanOpenOutputFolder(*full) &&
					!unavailable.OpenOutputFolder(fullId, failure) && !failure.empty(),
					"A host without a Shell callback retains valid results but disables folder opening");
			}

			settings.m_OutputDirectory.clear();
			settings.m_Timing = FrameCaptureTiming::AfterReady;
			settings.m_SettleFrames = 2;
			const uint64_t settledId = frontEnd.ExportGraph(true);
			for (uint64_t serial = 18; serial < 20; ++serial)
			{
				coordinator.BeginFrame(MakeFrameState(serial, 2));
				context.Check(control.m_Issued.empty(), "After-ready interactive graph export waits for settle frames");
				coordinator.OnFrameSubmitted();
			}
			coordinator.BeginFrame(MakeFrameState(20, 2));
			coordinator.OnFrameSubmitted();
			coordinator.Update();
			frontEnd.Update();
			const auto* settled = frontEnd.FindResult(settledId);
			context.Check(settled && settled->m_Status == FrameCaptureRequestStatus::Completed &&
				settled->m_Metadata->m_Timing == FrameCaptureTiming::AfterReady &&
				settled->m_Metadata->m_SettleFrames == 2 && settled->m_Metadata->m_FrameSerial == 20,
				"Graph buttons honor the selected after-ready timing rather than forcing next-frame");

			settings.m_Timing = FrameCaptureTiming::NextFrame;
			settings.m_ReferenceViewId = "CAM_Test";
			settings.m_OutputDirectory = directory / "resources";
			std::vector<RGResourceAddress> targets{
				{ .m_Resource = "Test.Depth", .m_Position = RGObservationPosition::Before,
					.m_Pass = "Test.Pass", .m_Occurrence = 2, .m_Mip = 1, .m_FirstSlice = 2,
					.m_SliceCount = 3, .m_Aspect = RHITextureAspect::Depth },
				{ .m_Resource = "Test.Buffer" },
			};
			const uint64_t resourceId = frontEnd.ExportResources(targets, true);
			targets.front().m_Resource = "Edited.After.Submission";
			const auto resourceView = coordinator.GetPendingViewChange();
			context.Check(resourceView && resourceView->m_RequestId == resourceId &&
				resourceView->m_ReferenceViewId == "CAM_Test" && settings.m_Source == FrameCaptureSource::Composited,
				"Interactive resource exports share the queued camera path without changing the screenshot source");
			coordinator.OnReferenceViewApplied(resourceId, true);
			coordinator.BeginFrame(MakeFrameState(21, 2));
			context.Check(control.m_Exports.size() == 1 && control.m_Exports[0].m_IncludeGraph &&
				control.m_Exports[0].m_Targets.size() == 2 && control.m_Exports[0].m_Targets[0].m_Resource == "Test.Depth" &&
				control.m_Exports[0].m_Targets[0].m_Position == RGObservationPosition::Before &&
				control.m_Exports[0].m_Targets[0].m_Occurrence == 2 && control.m_Exports[0].m_Targets[0].m_Mip == 1 &&
				control.m_Exports[0].m_Targets[0].m_FirstSlice == 2 && control.m_Exports[0].m_Targets[0].m_SliceCount == 3 &&
				control.m_Exports[0].m_Targets[0].m_Aspect == RHITextureAspect::Depth,
				"The manual export queues a complete address copy, including pass occurrence, subresources and plane");
			coordinator.OnFrameSubmitted();
			coordinator.Update();
			frontEnd.Update();
			const auto* resources = frontEnd.FindResult(resourceId);
			context.Check(resources && resources->m_Status == FrameCaptureRequestStatus::Completed &&
				resources->m_ResourcePaths.size() == 2 && !resources->m_GraphPath.empty() && resources->m_ImagePath.empty() &&
				ApplicationFrameCapture::GetOutputPath(*resources) == resources->m_MetadataPath,
				"Manual resource groups publish arrays, sidecar and paired graph through the existing writer");
			if (resources && resources->m_Status == FrameCaptureRequestStatus::Completed)
			{
				const auto json = ReadJson(resources->m_MetadataPath);
				const auto graph = ReadJson(resources->m_GraphPath);
				context.Check(!json.is_discarded() && !graph.is_discarded() &&
					json["frameSerial"] == 21 && graph["frameSerial"] == 21 && json["targets"].size() == 2 &&
					json["observation"]["graph"] == true && json["observation"]["resourceExport"] == true &&
					resources->m_Metadata->m_Label == "interactive" && resources->m_Metadata->m_Note == "shared settings" &&
					resources->m_Metadata->m_Camera.m_ReferenceViewId == "CAM_Test" &&
					std::filesystem::exists(resources->m_ResourcePaths.front()),
					"Manual export results retain the actual captured frame, shared settings and paired graph identity");
				settings.m_OutputDirectory = directory / "later setting";
				context.Check(frontEnd.OpenOutputFolder(resourceId, failure) && failure.empty() &&
					openedDirectory == directory / "resources",
					"Resource export folder opening uses the published group directory");
			}
			settings.m_ReferenceViewId.clear();
			settings.m_Timing = FrameCaptureTiming::AfterReady;
			settings.m_SettleFrames = 2;
			const uint64_t readyResourceId = frontEnd.ExportResources({ { .m_Resource = "Test.Buffer" } });
			for (uint64_t serial = 22; serial < 24; ++serial)
			{
				coordinator.BeginFrame(MakeFrameState(serial, 3));
				context.Check(control.m_Issued.empty(), "After-ready manual resource export waits for its settle frames");
				coordinator.OnFrameSubmitted();
			}
			coordinator.BeginFrame(MakeFrameState(24, 3));
			coordinator.OnFrameSubmitted();
			coordinator.Update();
			frontEnd.Update();
			const auto* readyResources = frontEnd.FindResult(readyResourceId);
			context.Check(readyResources && readyResources->m_Status == FrameCaptureRequestStatus::Completed &&
				readyResources->m_Metadata->m_FrameSerial == 24 && readyResources->m_Metadata->m_SettleFrames == 2 &&
				readyResources->m_GraphPath.empty(),
				"Manual resource export honors after-ready timing and can omit the paired graph");
			settings.m_Timing = FrameCaptureTiming::NextFrame;
			control.m_FailResourceExport = true;
			const uint64_t failedResourceId = frontEnd.ExportResources({ { .m_Resource = "Missing" } });
			coordinator.BeginFrame(MakeFrameState(25, 3));
			coordinator.OnFrameSubmitted();
			coordinator.Update();
			frontEnd.Update();
			const auto* failedResources = frontEnd.FindResult(failedResourceId);
			context.Check(failedResources && failedResources->m_Status == FrameCaptureRequestStatus::Failed &&
				failedResources->m_ResourceFailure == RGObservationFailure::UnknownResource &&
				!failedResources->m_Failure.empty() && failedResources->m_ResourcePaths.empty() &&
				!frontEnd.CanOpenOutputFolder(*failedResources),
				"An address missing in the actual frame retains its typed failure in manual export history");

			// Activate the real ImGui widgets by ID, without a native window or
			// pixel-coordinate assumptions. ImGui internals stay in this host test.
			{
				ImGuiContext* previousContext = ImGui::GetCurrentContext();
				ImGuiContext* gui = ImGui::CreateContext();
				ImGuiIO& io = ImGui::GetIO();
				io.IniFilename = nullptr;
				io.DisplaySize = ImVec2(1280.0f, 900.0f);
				unsigned char* pixels = nullptr;
				int width = 0;
				int height = 0;
				io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
				CaptureDiagnosticsView diagnostics;
				DevelopGuiStateStore store;
				DevelopGuiContext drawContext{ .m_Diagnostics = &diagnostics, .m_StateStore = &store };
				FrameCapturePanel panel(&frontEnd);
				const auto draw = [&]()
					{
						ImGui::NewFrame();
						ImGui::Begin("Manual resource capture test");
						panel.Draw(drawContext);
						ImGui::End();
						ImGui::Render();
					};
				draw();
				draw();
				ImGuiWindow* window = ImGui::FindWindowByName("Manual resource capture test");
				ImGuiTabBar* tabs = window ? gui->TabBars.GetByKey(window->GetID("##CaptureActions")) : nullptr;
				context.Check(tabs && tabs->Tabs.Size == 3, "The capture panel separates screenshot, graph and resource actions");
				if (tabs)
				{
					const ImGuiID resourceTabId = ImHashStr("Resources", 0, tabs->ID);
					tabs->NextSelectedTabId = resourceTabId;
					draw();
					const ImGuiID choicesId = ImHashStr("##AvailableResources", 0, resourceTabId);
					ImGuiTable* choices = ImGui::TableFindByID(choicesId);
					context.Check(choices && choices->CurrentRow == 1,
						"The resource list hides unsupported imported previews and shows only exportable rows");
					const ImGuiID rowId = ImHashStr("Test.UI.Texture", 0, choicesId);
					const ImGuiID plusId = ImHashStr("+", 0, rowId);
					control.m_FailResourceExport = false;
					ImGui::ActivateItemByID(plusId);
					draw();
					draw();
					const ImGuiID targetsId = ImHashStr("##ResourceTargets", 0, resourceTabId);
					const int firstTarget = 1;
					const ImGuiID targetId = ImHashData(&firstTarget, sizeof(firstTarget), targetsId);
					const ImGuiID addressTableId = ImHashStr("##TargetAddress", 0, targetId);
					const auto select = [&](const char* combo, const char* choice, int choiceIndex = -1)
						{
							ImGui::ActivateItemByID(ImHashStr(combo, 0, addressTableId));
							draw();
							if (gui->OpenPopupStack.empty() || !gui->OpenPopupStack.back().Window)
							{
								return false;
							}
							ImGuiWindow* popup = gui->OpenPopupStack.back().Window;
							const ImGuiID choiceId = choiceIndex < 0 ? popup->GetID(choice)
								: ImHashStr(choice, 0, popup->GetID(choiceIndex));
							ImGui::ActivateItemByID(choiceId);
							draw();
							return gui->OpenPopupStack.empty();
						};
					const bool selected = select("##Position", "After pass", 2) && select("##Pass", "Test.UI.Writer [#2]");
					context.Check(selected, "A plus-added target exposes its own pass and declaration occurrence controls");
					ImGui::ActivateItemByID(plusId);
					draw();
					const uint32_t pendingBefore = frontEnd.GetUnfinishedRequestCount();
					ImGui::ActivateItemByID(ImHashStr("Export Resources", 0, resourceTabId));
					draw();
					context.Check(frontEnd.GetUnfinishedRequestCount() == pendingBefore + 1,
						"The actual Export Resources button submits the added target through the front end");
					coordinator.BeginFrame(MakeFrameState(26, 3));
					context.Check(control.m_Exports.size() == 1 && control.m_Exports[0].m_Targets.size() == 2 &&
						control.m_Exports[0].m_Targets[0].m_Resource == "Test.UI.Texture" &&
						control.m_Exports[0].m_Targets[0].m_Position == RGObservationPosition::After &&
						control.m_Exports[0].m_Targets[0].m_Pass == "Test.UI.Writer" &&
						control.m_Exports[0].m_Targets[0].m_Occurrence == 2 &&
						control.m_Exports[0].m_Targets[1].m_Position == RGObservationPosition::Final,
						"Repeated plus additions retain independent addresses and declaration occurrences for the same resource");
					coordinator.OnFrameSubmitted();
					coordinator.Update();
					frontEnd.Update();
					draw();
					context.Check(frontEnd.GetHistory().back().m_Result.m_Status == FrameCaptureRequestStatus::Completed,
						"The real resource tab draws completed array history without opening the Shell");
					diagnostics.AddResources(40);
					draw();
					draw();
					choices = ImGui::TableFindByID(choicesId);
					context.Check(choices && choices->InnerWindow->ScrollMax.y > 0.0f &&
						choices->InnerWindow->Size.y <= ImGui::GetTextLineHeightWithSpacing() * 9.0f + 1.0f,
						"A long resource list scrolls internally and retains its fixed height");
					ImGui::ActivateItemByID(ImHashStr("##ResourceFilter", 0, resourceTabId));
					draw();
					io.AddInputCharactersUTF8("Test.UI.Texture");
					draw();
					choices = ImGui::TableFindByID(choicesId);
					context.Check(choices && choices->CurrentRow == 1,
						"Resource name search filters the available list without changing queued targets");
					ImGui::ActivateItemByID(ImHashStr("###ResourceChoices", 0, resourceTabId));
					draw();
					context.Check(choices && choices->LastFrameActive < gui->FrameCount,
						"The available resource list collapses while export targets remain accessible");
					const int secondTarget = 2;
					const ImGuiID secondId = ImHashData(&secondTarget, sizeof(secondTarget), targetsId);
					ImGui::ActivateItemByID(ImHashStr("X", 0, secondId));
					draw();
					ImGui::ActivateItemByID(ImHashStr("Export Resources", 0, resourceTabId));
					draw();
					coordinator.BeginFrame(MakeFrameState(27, 3));
					context.Check(control.m_Exports.size() == 1 && control.m_Exports[0].m_Targets.size() == 1 &&
						control.m_Exports[0].m_Targets[0].m_Occurrence == 2,
						"Removing one target retains the other target's independently configured address");
					coordinator.OnFrameSubmitted();
					coordinator.Update();
					frontEnd.Update();
				}
				ImGui::DestroyContext(gui);
				ImGui::SetCurrentContext(previousContext);
			}

			const uint32_t countBeforeCancellation = openCount;
			for (size_t index = 0; index < ApplicationFrameCapture::MaxHistoryEntries; ++index)
			{
				const uint64_t id = frontEnd.ExportGraph();
				frontEnd.Cancel(id);
				frontEnd.Update();
			}
			context.Check(frontEnd.GetHistory().size() == ApplicationFrameCapture::MaxHistoryEntries &&
				!frontEnd.FindResult(fullId) && !frontEnd.OpenOutputFolder(fullId, failure) &&
				!frontEnd.CanOpenOutputFolder(frontEnd.GetHistory().back().m_Result) &&
				openCount == countBeforeCancellation,
				"Cancelled or evicted results cannot launch folders and history stays bounded");
			coordinator.FinalizeAfterRenderHost();
		}

		RunReferenceViewBatchTests(context, directory / "batch-scene", FrameCaptureSource::Scene);
		RunReferenceViewBatchTests(context, directory / "batch-composited", FrameCaptureSource::Composited);

		// Invalid inputs are rejected before Shell/COM use, so headless tests do
		// not create Explorer windows. Successful desktop launch is a manual gate.
		std::string failure;
		context.Check(!win32::OpenDirectory({}, failure) && !failure.empty() &&
			!win32::OpenDirectory(directory / "missing", failure) && !failure.empty(),
			"The Windows adapter reports empty and deleted directories without launching Explorer");
		const auto file = directory / "not a folder.txt";
		{
			std::ofstream output(file);
			output << "test";
		}
		context.Check(!win32::OpenDirectory(file, failure) && !failure.empty(),
			"The Windows adapter refuses a file instead of invoking its associated application");
		std::filesystem::remove_all(directory.parent_path(), error);
	}
}
