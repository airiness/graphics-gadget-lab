#pragma once
#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	class GGLabAppRuntime;
	// Interactive owner for F9, panel exports and one sequential reference-view batch.
	class ApplicationFrameCapture final
	{
	public:
		struct Settings
		{
			CaptureImageSource m_Source = CaptureImageSource::Scene;
			FrameCaptureTiming m_Timing = FrameCaptureTiming::NextFrame;
			uint32_t m_SettleFrames = 0;
			std::string m_ReferenceViewId;
			std::filesystem::path m_OutputDirectory;
			std::string m_Label;
			std::string m_Note;
		};
		struct HistoryEntry
		{
			// Consumed receipt metadata/files only; no Runtime payload or reservation.
			CaptureJobResult m_Result;
			std::chrono::steady_clock::time_point m_FinishedAt;
		};
		enum class ReferenceViewBatchState : uint8_t
		{
			Idle,
			Running,
			Completed,
			Failed,
			Cancelled,
		};
		struct ReferenceViewBatch
		{
			ReferenceViewBatchState m_State = ReferenceViewBatchState::Idle;
			uint32_t m_TotalViews = 0;
			uint32_t m_AcceptedViews = 0;
			uint32_t m_CompletedViews = 0;
			uint64_t m_JobId = 0;
		};

		using OpenDirectoryCallback = std::function<bool(const std::filesystem::path&, std::string&)>;
		struct CreateInfo
		{
			// Borrowed diagnostics only. Job ownership goes through the owner callbacks.
			FrameCaptureCoordinator* m_Coordinator = nullptr;
			std::filesystem::path m_DefaultOutputDirectory;
			std::function<CaptureAdmission(CapturePlan&&)> m_TrySubmit;
			std::function<CaptureCancelResult(uint64_t)> m_Cancel;
			std::function<std::optional<CaptureJobResult>(uint64_t)> m_TakeResult;
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
		CaptureAdmission Capture() noexcept;
		CaptureAdmission ExportGraph(bool summary = false) noexcept;
		CaptureAdmission ExportResources(std::vector<RGResourceAddress> targets,
			bool includeGraph = false) noexcept;
		CaptureAdmission Submit(CapturePlan&& plan) noexcept;
		CaptureCancelResult Cancel(uint64_t jobId) noexcept;
		// Snapshot authored views/settings; hold at most one accepted batch job.
		// Busy retries on Update; rejection or a failed product stops admission.
		uint32_t CaptureReferenceViews() noexcept;
		CaptureCancelResult CancelReferenceViews() noexcept;
		[[nodiscard]] const ReferenceViewBatch& GetReferenceViewBatch() const noexcept { return m_Batch; }
		[[nodiscard]] CaptureAdmission GetLastAdmission() const noexcept { return m_LastAdmission; }
		[[nodiscard]] std::chrono::steady_clock::time_point GetLastAdmissionTime() const noexcept
		{
			return m_LastAdmissionTime;
		}
		[[nodiscard]] const CaptureJobResult* FindResult(uint64_t jobId) const noexcept;
		// Consume owned receipts into bounded display history; shutdown disables admission.
		uint32_t Update(bool admitReferenceViews = true) noexcept;
		[[nodiscard]] const std::deque<HistoryEntry>& GetHistory() const noexcept { return m_History; }
		[[nodiscard]] uint32_t GetPendingJobCount() const noexcept;
		[[nodiscard]] const FrameCaptureFrameState* GetLastFrameState() const noexcept;
		[[nodiscard]] uint32_t GetSettledFrameCount() const noexcept;

		[[nodiscard]] static bool IsPublished(const CaptureJobResult& result) noexcept;
		[[nodiscard]] static const std::filesystem::path& GetOutputPath(const CaptureJobResult& result) noexcept;
		[[nodiscard]] static const FrameCaptureMetadata* GetMetadata(const CaptureJobResult& result) noexcept;
		[[nodiscard]] static std::string_view GetFailure(const CaptureJobResult& result) noexcept;
		[[nodiscard]] static std::string GetPathText(const std::filesystem::path& path) noexcept;
		[[nodiscard]] static std::string_view GetAdmissionReasonName(CaptureAdmissionReason reason) noexcept;
		[[nodiscard]] static std::string_view GetPublicationName(CaptureJobPublication publication) noexcept;
		[[nodiscard]] static std::string_view GetCancellationName(CaptureCancelResult cancellation) noexcept;
		[[nodiscard]] bool CanOpenOutputFolder(const CaptureJobResult& result) const noexcept;
		bool OpenOutputFolder(uint64_t jobId, std::string& failure) const noexcept;

	private:
		[[nodiscard]] CapturePlan MakePlan() const noexcept;
		void AdvanceReferenceViews() noexcept;

		FrameCaptureCoordinator* m_Coordinator = nullptr;
		std::function<CaptureAdmission(CapturePlan&&)> m_TrySubmit;
		std::function<CaptureCancelResult(uint64_t)> m_Cancel;
		std::function<std::optional<CaptureJobResult>(uint64_t)> m_TakeResult;
		OpenDirectoryCallback m_OpenDirectory;
		std::filesystem::path m_DefaultOutputDirectory;
		Settings m_Settings;
		std::vector<uint64_t> m_PendingJobs;
		std::deque<HistoryEntry> m_History;
		CaptureAdmission m_LastAdmission{ CaptureAdmissionStatus::Accepted };
		std::chrono::steady_clock::time_point m_LastAdmissionTime{};
		ReferenceViewBatch m_Batch;
		CapturePlan m_BatchPlan;
		std::vector<std::string> m_BatchViews;
	};
}
