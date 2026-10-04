#pragma once
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/FrameCaptureReadiness.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gglab
{
	class FrameCaptureControlBase;
	class TaskSystem;

	struct FrameCaptureRequest
	{
		FrameCaptureSource m_Source = FrameCaptureSource::Scene;
		FrameCaptureTiming m_Timing = FrameCaptureTiming::NextFrame;
		// AfterReady only: frames rendered with every gate ready and an unchanged
		// settle key before the captured frame.
		uint32_t m_SettleFrames = 0;
		// AfterReady only: when set, the capture also waits until the active Demo
		// id or Lab id equals this id.
		std::string m_RequiredContentId;
		// Empty selects the coordinator's default output directory.
		std::filesystem::path m_OutputDirectory;
		std::string m_Label;
		std::string m_Note;
	};

	enum class FrameCaptureRequestStatus : uint8_t
	{
		// The PNG and its metadata sidecar are completely written.
		Completed,
		Failed,
		Cancelled,
	};

	struct FrameCaptureRequestResult
	{
		uint64_t m_RequestId = 0;
		FrameCaptureRequestStatus m_Status = FrameCaptureRequestStatus::Failed;
		std::filesystem::path m_ImagePath;
		std::filesystem::path m_MetadataPath;
		std::string m_Failure;
		// Present once the capture was issued to a frame.
		std::optional<FrameCaptureMetadata> m_Metadata;
	};

	// Identity of the temporal state a settled-frame count accumulates over. Any
	// change restarts settling, because temporal history no longer describes the
	// view being captured.
	struct FrameCaptureSettleKey
	{
		uint64_t m_TemporalSession = 0;
		uint64_t m_CameraResetSerial = 0;
		uint32_t m_DisplayView = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_DemoIndex = 0;

		bool operator==(const FrameCaptureSettleKey&) const noexcept = default;
	};

	// Runtime state of the frame about to be built, supplied by the application
	// runtime once per rendered frame.
	struct FrameCaptureFrameState
	{
		std::string m_Backend;
		std::string m_DemoId;
		std::string m_LabId;
		FrameCaptureReadiness m_Readiness{};
		FrameCaptureSettleKey m_SettleKey{};
		uint64_t m_FrameIndex = 0;
		FrameCaptureCameraState m_Camera{};
		std::optional<double> m_FixedDeltaTime;
		double m_TotalTime = 0.0;
		bool m_DevelopmentTools = false;
	};

	// Turns host capture requests into Runtime frame captures at the requested
	// time, then encodes the PNG and writes the metadata sidecar off the frame
	// thread. Every submitted request finishes with exactly one result.
	class FrameCaptureCoordinator final
	{
	public:
		struct CreateInfo
		{
			FrameCaptureControlBase* m_Capture = nullptr;
			// Null encodes on the calling thread.
			TaskSystem* m_TaskSystem = nullptr;
			std::filesystem::path m_DefaultOutputDirectory;
		};

		explicit FrameCaptureCoordinator(const CreateInfo& createInfo) noexcept;
		~FrameCaptureCoordinator();
		GGLAB_DELETE_COPYABLE_MOVABLE(FrameCaptureCoordinator);

		// Returns the non-zero request id.
		[[nodiscard]] uint64_t Submit(FrameCaptureRequest request) noexcept;
		// Cancels a request that has not been issued to a frame yet.
		bool Cancel(uint64_t requestId) noexcept;
		// Appends finished results in completion order.
		void ConsumeResults(std::vector<FrameCaptureRequestResult>& outResults) noexcept;
		[[nodiscard]] uint32_t GetUnfinishedRequestCount() const noexcept;
		// State of the most recent frame passed to BeginFrame; null before the
		// first rendered frame.
		[[nodiscard]] const FrameCaptureFrameState* GetLastFrameState() const noexcept;
		// Consecutive submitted frames with every gate ready and an unchanged
		// settle key.
		[[nodiscard]] uint32_t GetSettledFrameCount() const noexcept { return m_SettledFrames; }

		// Called before the frame described by the state is built. Issues every
		// request that is due so that this frame's capture taps record it.
		void BeginFrame(FrameCaptureFrameState state) noexcept;
		// Called after the frame passed to BeginFrame was submitted.
		void OnFrameSubmitted() noexcept;
		// Collects Runtime capture results, starts encoding and publishes
		// finished requests.
		void Update() noexcept;
		// Called while the task system still runs: cancels requests that were not
		// issued and waits for in-flight encoding.
		void PrepareForShutdown() noexcept;
		// Called after the render host finalized and before it is destroyed:
		// encodes the final Runtime results on the calling thread and fails
		// anything left. Afterwards no request is unfinished.
		void FinalizeAfterRenderHost() noexcept;

	private:
		struct EncodeJob;

		enum class Phase : uint8_t
		{
			Waiting,
			Issued,
			Encoding,
			Finished,
		};

		struct Entry
		{
			uint64_t m_Id = 0;
			FrameCaptureRequest m_Request{};
			Phase m_Phase = Phase::Waiting;
			uint64_t m_CaptureRequestId = 0;
			FrameCaptureMetadata m_Metadata{};
			std::shared_ptr<EncodeJob> m_Job;
		};

		[[nodiscard]] bool IsDue(const Entry& entry, const FrameCaptureFrameState& state,
			bool ready) const noexcept;
		void Issue(Entry& entry, const FrameCaptureFrameState& state) noexcept;
		void HandleCaptureResult(FrameCaptureResult result) noexcept;
		void StartEncoding(Entry& entry, const FrameCaptureResult& result) noexcept;
		void CollectEncodedJobs(bool wait) noexcept;
		void Finish(Entry& entry, FrameCaptureRequestStatus status, std::string failure) noexcept;
		void RemoveFinishedEntries() noexcept;
		static void RunEncodeJob(EncodeJob& job) noexcept;

		FrameCaptureControlBase* m_Capture = nullptr;
		TaskSystem* m_TaskSystem = nullptr;
		std::filesystem::path m_DefaultOutputDirectory;
		uint64_t m_NextRequestId = 1;
		std::vector<Entry> m_Entries;
		std::vector<FrameCaptureRequestResult> m_Results;
		// Image paths of captures still being encoded, so concurrent captures never
		// select the same file name.
		std::vector<std::filesystem::path> m_ReservedPaths;
		std::optional<FrameCaptureFrameState> m_LastFrameState;
		uint32_t m_SettledFrames = 0;
		bool m_FrameReady = false;
		bool m_HasOpenFrame = false;
		bool m_IsShuttingDown = false;
	};
}
