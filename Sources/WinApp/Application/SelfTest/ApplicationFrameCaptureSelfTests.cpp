#include "Application/SelfTest/ApplicationFrameCaptureSelfTests.h"
#include "Application/Capture/ApplicationFrameCapture.h"
#include "Application/Platform/Windows/Win32FrameCaptureImageEncoder.h"
#include "Application/Platform/Windows/Win32Shell.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiStateStore.h"
#include "DevTools/DevelopGui/Panels/FrameCapturePanel.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"

#include <nlohmann/json.hpp>
#include <imgui.h>

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
			uint64_t RequestCapture(FrameCaptureSource source, uint64_t frameSerial) noexcept override
			{
				m_Issued.push_back({ .m_RequestId = m_NextId++, .m_Source = source,
					.m_Status = FrameCaptureStatus::Completed, .m_FrameSerial = frameSerial });
				return m_Issued.back().m_RequestId;
			}
			uint64_t RequestDiagnosticCapture(PostProcessDebugTap, uint64_t) noexcept override { return 0; }
			std::optional<PostProcessDebugTap> GetPendingDiagnosticTap() const noexcept override { return std::nullopt; }
			FrameCaptureAvailability GetAvailability(RHIFormat, uint32_t, uint32_t) const noexcept override
			{
				return FrameCaptureAvailability::Available;
			}
			uint32_t GetUnfinishedRequestCount() const noexcept override { return 0; }
			void ConsumeResults(std::vector<FrameCaptureResult>& results) noexcept override
			{
				for (FrameCaptureResult result : m_Issued)
				{
					if (result.m_Source == FrameCaptureSource::RenderGraph)
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
			}

			std::vector<FrameCaptureResult> m_Issued;

		private:
			uint64_t m_NextId = 100;
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
	}

	void RunApplicationFrameCaptureSelfTests(SelfTestContext& context) noexcept
	{
		std::error_code error;
		const std::filesystem::path directory = std::filesystem::temp_directory_path(error) /
			std::format("gglab-interactive-capture-{}", std::chrono::steady_clock::now().time_since_epoch().count()) /
			std::filesystem::path(u8"\u4e2d\u6587 captures");
		std::filesystem::create_directories(directory, error);
		context.Check(!error, "Interactive capture tests create a Unicode output directory with spaces");
		if (error) return;

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
					{ coordinator.ConsumeResults(results); },
				.m_OpenDirectory = [&](const std::filesystem::path& path, std::string& failure)
					{
						++openCount;
						openedDirectory = path;
						if (rejectOpen) failure = "Injected Shell rejection.";
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
				// Draw the actual panel without a native window. Wrapped help must not
				// collapse the first-use window into an unusable narrow strip.
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
							"The first-use capture panel has usable space for buttons and wrapped export help");
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
						{ if (std::exchange(pending, false)) output.push_back(result); } });
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

		// Invalid inputs are rejected before Shell/COM use, so headless tests do
		// not create Explorer windows. Successful desktop launch is a manual gate.
		std::string failure;
		context.Check(!win32::OpenDirectory({}, failure) && !failure.empty() &&
			!win32::OpenDirectory(directory / "missing", failure) && !failure.empty(),
			"The Windows adapter reports empty and deleted directories without launching Explorer");
		const auto file = directory / "not a folder.txt";
		{ std::ofstream output(file); output << "test"; }
		context.Check(!win32::OpenDirectory(file, failure) && !failure.empty(),
			"The Windows adapter refuses a file instead of invoking its associated application");
		std::filesystem::remove_all(directory.parent_path(), error);
	}
}
