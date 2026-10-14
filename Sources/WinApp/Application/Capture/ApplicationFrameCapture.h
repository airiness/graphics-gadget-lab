#pragma once
#include "Capture/FrameCaptureCoordinator.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGObservation.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace gglab
{
	class GGLabAppRuntime;
	// Interactive capture front end shared by the capture hotkey and the
	// DevelopGui capture panel. It submits requests with the current interactive
	// settings, logs every finished capture and keeps a short display history.
	class ApplicationFrameCapture final
	{
	public:
		struct Settings
		{
			FrameCaptureSource m_Source = FrameCaptureSource::Scene;
			FrameCaptureTiming m_Timing = FrameCaptureTiming::NextFrame;
			uint32_t m_SettleFrames = 0;
			// Empty keeps the current camera.
			std::string m_ReferenceViewId;
			// Empty selects the runtime's default capture directory.
			std::filesystem::path m_OutputDirectory;
			std::string m_Label;
			std::string m_Note;
		};

		struct HistoryEntry
		{
			FrameCaptureRequestResult m_Result{};
			std::chrono::steady_clock::time_point m_FinishedAt{};
		};

		// Returns whether the host accepted the open request; failures are separate
		// from capture results and never change their publication status.
		using OpenDirectoryCallback = std::function<bool(const std::filesystem::path&, std::string&)>;

		struct CreateInfo
		{
			// Borrowed; the coordinator and result publisher outlive this front end.
			FrameCaptureCoordinator* m_Coordinator = nullptr;
			std::filesystem::path m_DefaultOutputDirectory;
			// Drain the owner's published stream, not the coordinator when AppRuntime
			// owns it: sequences must receive results before the host consumes them.
			std::function<void(std::vector<FrameCaptureRequestResult>&)> m_ConsumeResults;
			OpenDirectoryCallback m_OpenDirectory;
		};

		static constexpr size_t MaxHistoryEntries = 16;

		explicit ApplicationFrameCapture(CreateInfo createInfo) noexcept;
		ApplicationFrameCapture(GGLabAppRuntime& runtime,
			std::filesystem::path defaultOutputDirectory,
			OpenDirectoryCallback openDirectory = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ApplicationFrameCapture);
		~ApplicationFrameCapture() = default;

		[[nodiscard]] Settings& GetSettings() noexcept { return m_Settings; }
		[[nodiscard]] const Settings& GetSettings() const noexcept { return m_Settings; }
		[[nodiscard]] const std::filesystem::path& GetDefaultOutputDirectory() const noexcept
		{
			return m_DefaultOutputDirectory;
		}

		// Submits a capture with the current settings and returns its request id.
		uint64_t Capture() noexcept;
		// Uses the same timing, view and output settings without changing the
		// screenshot source used by Capture and F9. Default timing is next frame.
		uint64_t ExportGraph(bool summary = false) noexcept;
		// Copies an address group into the existing resource export queue. The live
		// compiled frame resolves the names; discovery snapshots are only UI choices.
		uint64_t ExportResources(std::vector<RGResourceAddress> targets,
			bool includeGraph = false) noexcept;
		// Submits one capture per reference view of the active content, in order,
		// with the current settings, waiting for temporary capacity before changing
		// views or issuing frames. Returns the number of submitted requests.
		uint32_t CaptureReferenceViews() noexcept;
		// Submits an explicit request and returns its request id.
		uint64_t Submit(FrameCaptureRequest request) noexcept;
		// Cancels a request that has not been issued to a frame yet.
		bool Cancel(uint64_t requestId) noexcept;
		// Finished result of a request still held in the history, or null.
		[[nodiscard]] const FrameCaptureRequestResult* FindResult(uint64_t requestId) const noexcept;
		// Moves finished captures into the history and returns them; call once per
		// host frame.
		std::vector<FrameCaptureRequestResult> Update() noexcept;

		// Newest entry last.
		[[nodiscard]] const std::deque<HistoryEntry>& GetHistory() const noexcept
		{
			return m_History;
		}
		[[nodiscard]] uint32_t GetUnfinishedRequestCount() const noexcept;
		[[nodiscard]] const FrameCaptureFrameState* GetLastFrameState() const noexcept;
		[[nodiscard]] uint32_t GetSettledFrameCount() const noexcept;

		// Images use their PNG; graph/resource exports use their published JSON.
		[[nodiscard]] static const std::filesystem::path& GetOutputPath(
			const FrameCaptureRequestResult& result) noexcept;
		// ImGui, clipboard and logs use UTF-8, not the Windows narrow path encoding.
		[[nodiscard]] static std::string GetPathText(const std::filesystem::path& path) noexcept;
		[[nodiscard]] bool CanOpenOutputFolder(const FrameCaptureRequestResult& result) const noexcept;
		// Only an explicit UI action calls this. Uses the retained completed result,
		// never the current editable output directory. Does not create directories.
		bool OpenOutputFolder(uint64_t requestId, std::string& failure) const noexcept;

	private:
		[[nodiscard]] FrameCaptureRequest MakeRequest(FrameCaptureSource source) const noexcept;

		FrameCaptureCoordinator* m_Coordinator = nullptr;
		std::function<void(std::vector<FrameCaptureRequestResult>&)> m_ConsumeResults;
		OpenDirectoryCallback m_OpenDirectory;
		std::filesystem::path m_DefaultOutputDirectory;
		Settings m_Settings{};
		std::deque<HistoryEntry> m_History;
	};
}
