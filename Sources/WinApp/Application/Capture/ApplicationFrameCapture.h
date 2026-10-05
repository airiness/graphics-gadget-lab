#pragma once
#include "Capture/FrameCaptureCoordinator.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace gglab
{
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

		static constexpr size_t MaxHistoryEntries = 16;

		ApplicationFrameCapture(FrameCaptureCoordinator& coordinator,
			std::filesystem::path defaultOutputDirectory) noexcept;
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

	private:
		FrameCaptureCoordinator* m_Coordinator = nullptr;
		std::filesystem::path m_DefaultOutputDirectory;
		Settings m_Settings{};
		std::deque<HistoryEntry> m_History;
	};
}
