#pragma once
#include "Capture/FrameCaptureImageEncoder.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/FrameCaptureReadiness.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gglab
{
	class FrameCaptureControlBase;

	struct FrameCaptureRequest
	{
		FrameCaptureSource m_Source = FrameCaptureSource::Scene;
		FrameCaptureTiming m_Timing = FrameCaptureTiming::NextFrame;
		// AfterReady only: frames rendered with every gate ready and an unchanged
		// settle key before the captured frame. The settle key's temporal history
		// must begin on a ready frame, so the capture never accumulates frames of
		// content that was still loading.
		uint32_t m_SettleFrames = 0;
		// AfterReady only: when set, the capture also waits until the active Demo
		// id or Lab id equals this id.
		std::string m_RequiredContentId;
		// When set, the runtime restores this camera reference view of the active
		// content before the capture. The view is applied only after every earlier
		// request was issued, and its camera cut restarts settling.
		std::string m_ReferenceViewId;
		// Empty selects the coordinator's default output directory.
		std::filesystem::path m_OutputDirectory;
		std::string m_Label;
		std::string m_Note;
		// Set by a camera-path sequence for the frame it captures.
		std::optional<FrameCaptureSequenceInfo> m_Sequence;
		// Required for Diagnostic captures and ignored by other sources.
		std::optional<PostProcessDebugTap> m_DiagnosticTap;
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
		// Reference views the active content registers for its main camera.
		std::vector<std::string> m_ReferenceViewIds;
		std::optional<double> m_FixedDeltaTime;
		double m_TotalTime = 0.0;
		bool m_DevelopmentTools = false;
		FrameCaptureTemporalState m_Temporal{};
	};

	// A camera reference view the runtime restores for a waiting request.
	struct FrameCaptureViewChange
	{
		uint64_t m_RequestId = 0;
		std::string m_ReferenceViewId;
	};

	// Turns host capture requests into Runtime frame captures at the requested
	// time, then encodes the PNG and writes the metadata sidecar on a dedicated
	// writer thread, so slow or blocked file I/O never occupies the shared task
	// system. At most eight jobs wait behind the running writer job; a capture
	// fails if the writer queue is full. Every request finishes with one result.
	class FrameCaptureCoordinator final
	{
	public:
		// Encode jobs that may wait behind the running writer job.
		static constexpr uint32_t MaxPendingWriteJobs = 8;

		struct CreateInfo
		{
			FrameCaptureControlBase* m_Capture = nullptr;
			std::filesystem::path m_DefaultOutputDirectory;
			// Without an encoder every capture fails when it is written.
			FrameCaptureImageEncoder m_ImageEncoder;
			// Total time shutdown waits for unfinished writing before failing the
			// remaining captures; only jobs before publication can be abandoned.
			std::chrono::milliseconds m_ShutdownWriteTimeout{ 30000 };
			// Writes on the calling thread instead of the writer thread, so a
			// result is published by the Update that collected the capture.
			bool m_WriteOnCallingThread = false;
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
		// settle key whose temporal history began on a ready frame.
		[[nodiscard]] uint32_t GetSettledFrameCount() const noexcept { return m_SettledFrames; }

		// Called before the next frame is planned, with the state of the previous
		// frame. Returns the reference view to restore on the active content's
		// main camera; the runtime reports the outcome to OnReferenceViewApplied.
		[[nodiscard]] std::optional<FrameCaptureViewChange> GetPendingViewChange() const noexcept;
		// A view that could not be restored fails its request.
		void OnReferenceViewApplied(uint64_t requestId, bool restored) noexcept;
		// Called before the next frame is planned, after any reference view was
		// restored. True when a waiting after-ready request cannot settle because
		// the temporal history of the ready previous frame includes frames rendered
		// before every gate was ready. The runtime then requests a temporal reset
		// of the display camera, and settling starts at that camera cut. Without
		// the cut, the captured image would depend on how many frames loading took.
		[[nodiscard]] bool ShouldRestartTemporalHistory() const noexcept;
		// Called before simulation time advances for the next frame. True while a
		// waiting after-ready request has not begun settling, so time-driven
		// content advances only over the settled frames, however long loading took.
		[[nodiscard]] bool ShouldHoldTime() const noexcept;
		// Called before the frame described by the state is built. Issues every
		// request that is due so that this frame's capture taps record it, and
		// fails waiting after-ready requests for content with a failed gate.
		void BeginFrame(FrameCaptureFrameState state) noexcept;
		// Called after the frame passed to BeginFrame was submitted.
		void OnFrameSubmitted() noexcept;
		// Collects Runtime capture results, starts encoding and publishes
		// finished requests.
		void Update() noexcept;
		// Called when the runtime begins shutting down: cancels requests that
		// were not issued. Captures already recorded keep being written.
		void PrepareForShutdown() noexcept;
		// Called after the render host finalized and before it is destroyed:
		// writes the final Runtime results and waits for writing up to the
		// shutdown write timeout in total. Unfinished captures fail. Before the
		// writer claims publication, a job is abandoned and never publishes files.
		// Once publication is claimed, its outcome is indeterminate: files may
		// already exist or appear later. Afterwards no request is unfinished.
		void FinalizeAfterRenderHost() noexcept;

	private:
		struct EncodeJob;
		struct WriterState;

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
			// The requested reference view was restored, and the camera reset
			// serial of the first frame rendered with it. A different serial later
			// means another camera cut replaced the view, which is then restored again.
			bool m_ViewApplied = false;
			std::optional<uint64_t> m_ViewCameraResetSerial;
			FrameCaptureMetadata m_Metadata{};
			std::shared_ptr<EncodeJob> m_Job;
		};

		[[nodiscard]] static bool MatchesRequiredContent(
			const Entry& entry, const FrameCaptureFrameState& state) noexcept;
		[[nodiscard]] bool IsDue(
			const Entry& entry, const FrameCaptureFrameState& state) const noexcept;
		void Issue(Entry& entry, const FrameCaptureFrameState& state) noexcept;
		void HandleCaptureResult(FrameCaptureResult result) noexcept;
		void StartEncoding(Entry& entry, const FrameCaptureResult& result) noexcept;
		// Publishes the result of a written job; false while it is unfinished.
		[[nodiscard]] bool TryFinishWritten(Entry& entry) noexcept;
		void CollectWrittenJobs() noexcept;
		// Waits for unfinished writing until the shutdown write timeout, then
		// fails what remains and abandons jobs that have not claimed publication.
		void FinishWriting() noexcept;
		void StopWriter() noexcept;
		void Finish(Entry& entry, FrameCaptureRequestStatus status, std::string failure) noexcept;
		void RemoveFinishedEntries() noexcept;
		static void RunWriter(std::shared_ptr<WriterState> state,
			FrameCaptureImageEncoder encoder) noexcept;
		static void RunEncodeJob(EncodeJob& job, const FrameCaptureImageEncoder& encoder) noexcept;
		[[nodiscard]] static std::string WriteAndPublish(
			EncodeJob& job, const FrameCaptureImageEncoder& encoder) noexcept;

		FrameCaptureControlBase* m_Capture = nullptr;
		std::filesystem::path m_DefaultOutputDirectory;
		FrameCaptureImageEncoder m_ImageEncoder;
		std::chrono::milliseconds m_ShutdownWriteTimeout{ 30000 };
		bool m_WriteOnCallingThread = false;
		// Shared with the writer thread, which keeps its own reference.
		std::shared_ptr<WriterState> m_Writer;
		std::thread m_WriterThread;
		// A writer with jobs unfinished at the shutdown timeout may be blocked in
		// file I/O; shutdown then leaves it to finish on its own.
		bool m_HasAbandonedJobs = false;
		uint64_t m_NextRequestId = 1;
		std::vector<Entry> m_Entries;
		std::vector<FrameCaptureRequestResult> m_Results;
		// Names this coordinator's temporary files apart from those of other
		// processes writing to the same directory.
		std::string m_TemporaryTag;
		std::optional<FrameCaptureFrameState> m_LastFrameState;
		uint32_t m_SettledFrames = 0;
		// Every frame of the current settle key so far had every gate ready.
		bool m_HistoryReady = false;
		bool m_HasOpenFrame = false;
		bool m_IsShuttingDown = false;
	};
}
