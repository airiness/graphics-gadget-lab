#pragma once
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/FrameCaptureReadiness.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gglab
{
	class FrameCaptureControlBase;

	// Encodes a captured image as the bytes of its PNG file; nullopt when the
	// image cannot be encoded. Called on the coordinator's writer thread.
	using FrameCaptureImageEncoder =
		std::function<std::optional<std::vector<uint8_t>>(const FrameCaptureImage&)>;

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
		// When set, the runtime restores this camera reference view of the active
		// content before the capture. The view is applied only after every earlier
		// request was issued, and its camera cut restarts settling.
		std::string m_ReferenceViewId;
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
		// Reference views the active content registers for its main camera.
		std::vector<std::string> m_ReferenceViewIds;
		std::optional<double> m_FixedDeltaTime;
		double m_TotalTime = 0.0;
		bool m_DevelopmentTools = false;
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
	// system. Every submitted request finishes with exactly one result.
	class FrameCaptureCoordinator final
	{
	public:
		struct CreateInfo
		{
			FrameCaptureControlBase* m_Capture = nullptr;
			std::filesystem::path m_DefaultOutputDirectory;
			// Empty selects the platform PNG encoder.
			FrameCaptureImageEncoder m_ImageEncoder;
			// Total time shutdown waits for unfinished writing before it abandons
			// the remaining captures.
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
		// settle key.
		[[nodiscard]] uint32_t GetSettledFrameCount() const noexcept { return m_SettledFrames; }

		// Called before the next frame is planned, with the state of the previous
		// frame. Returns the reference view to restore on the active content's
		// main camera; the runtime reports the outcome to OnReferenceViewApplied.
		[[nodiscard]] std::optional<FrameCaptureViewChange> GetPendingViewChange() const noexcept;
		// A view that could not be restored fails its request.
		void OnReferenceViewApplied(uint64_t requestId, bool restored) noexcept;
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
		// shutdown write timeout in total. A capture still unfinished then is
		// abandoned: it fails and its files are not published. Afterwards no
		// request is unfinished.
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
		[[nodiscard]] bool IsDue(const Entry& entry, const FrameCaptureFrameState& state,
			bool ready) const noexcept;
		void Issue(Entry& entry, const FrameCaptureFrameState& state) noexcept;
		void HandleCaptureResult(FrameCaptureResult result) noexcept;
		void StartEncoding(Entry& entry, const FrameCaptureResult& result) noexcept;
		// Publishes the result of a written job; false while it is unfinished.
		[[nodiscard]] bool TryFinishWritten(Entry& entry) noexcept;
		void CollectWrittenJobs() noexcept;
		// Waits for unfinished writing until the shutdown write timeout and
		// abandons what remains.
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
		// A writer that still holds an abandoned capture may be blocked in file
		// I/O; shutdown then leaves it to finish on its own.
		bool m_HasAbandonedJobs = false;
		uint64_t m_NextRequestId = 1;
		std::vector<Entry> m_Entries;
		std::vector<FrameCaptureRequestResult> m_Results;
		// Names this coordinator's temporary files apart from those of other
		// processes writing to the same directory.
		std::string m_TemporaryTag;
		std::optional<FrameCaptureFrameState> m_LastFrameState;
		uint32_t m_SettledFrames = 0;
		bool m_FrameReady = false;
		bool m_HasOpenFrame = false;
		bool m_IsShuttingDown = false;
	};
}
