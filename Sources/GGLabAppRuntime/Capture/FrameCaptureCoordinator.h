#pragma once
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "Capture/CapturePlan.h"
#include "Capture/CaptureJobTypes.h"
#include "Capture/FrameCaptureImageEncoder.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/FrameCaptureReadiness.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalHistoryTypes.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gglab
{
	class FrameCaptureControlBase;
	class NonFiniteWatchCoordinator;
	class CapturePublicationFileSystemBase;
	class CaptureWriter;
	struct CaptureProductEncodeInput;
	struct CaptureWriteReceipt;

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
		// Flexible batches may wait for temporary capacity before applying a view
		// or issuing a capture. Once issued, the frame is fixed. Exact sequence
		// frames ignore this option; permanent capacity errors still fail.
		bool m_WaitForCapacity = false;
		// Empty selects the coordinator's default output directory.
		std::filesystem::path m_OutputDirectory;
		std::string m_Label;
		std::string m_Note;
		// RenderGraph requests only: write the discovery summary instead of full records.
		bool m_GraphSummary = false;
		std::vector<RGResourceAddress> m_ResourceTargets;
		bool m_IncludeGraph = false;
		// Legacy provenance only; matching sequence values do not associate requests.
		std::optional<FrameCaptureSequenceInfo> m_Sequence;
		// Required for Diagnostic captures and ignored by other sources.
		std::optional<PostProcessDebugTap> m_DiagnosticTap;
	};

	enum class FrameCaptureRequestStatus : uint8_t
	{
		// The output and its existing completion marker are completely written.
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
		std::vector<std::filesystem::path> m_ResourcePaths;
		std::filesystem::path m_GraphPath;
		RGObservationFailure m_ResourceFailure = RGObservationFailure::None;
		std::string m_Failure;
		// Present once the capture was issued to a frame.
		std::optional<FrameCaptureMetadata> m_Metadata;
	};

	// Runtime owns temporal compatibility; AppRuntime adds the active content.
	// A changed identity or unreadable previous history restarts settling.
	struct FrameCaptureSettleKey
	{
		TemporalFrameContinuity m_Temporal{};
		uint32_t m_DemoIndex = 0;

		[[nodiscard]] bool Continues(const FrameCaptureSettleKey& previous,
			bool allowCameraCut = false) const noexcept
		{
			return m_DemoIndex == previous.m_DemoIndex &&
				m_Temporal.Continues(previous.m_Temporal, allowCameraCut);
		}
	};

	// Runtime state of the frame about to be built, supplied by the application
	// runtime once per rendered frame.
	struct FrameCaptureFrameState
	{
		std::string m_Backend;
		std::string m_DemoId;
		std::string m_LabId;
		std::optional<FrameCaptureAssetBundle> m_AssetBundle;
		FrameCaptureReadiness m_Readiness{};
		FrameCaptureSettleKey m_SettleKey{};
		// Actual presentation format; Diagnostic captures use RGBA8 instead.
		RHIFormat m_DisplayFormat = RHIFormat::Unknown;
		uint64_t m_FrameSerial = 0;
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

	// Schedules required SameFrame products, then hands immutable CPU products to
	// a dedicated writer. Nine output reservations cover GPU issue through writing:
	// one running writer and eight pending jobs. Transitional IDs/results and file
	// completion markers remain per legacy output, not an atomic multi-file bundle.
	class FrameCaptureCoordinator final
	{
	public:
		// Encode jobs that may wait behind the running writer job.
		static constexpr uint32_t MaxPendingWriteJobs = 8;
		static constexpr uint32_t MaxInFlightCaptures = MaxPendingWriteJobs + 1;

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
			NonFiniteWatchCoordinator* m_NonFiniteWatch = nullptr;
			// Optional host-neutral file capability; the writer retains it independently.
			std::shared_ptr<CapturePublicationFileSystemBase> m_PublicationFileSystem;
			CaptureJobLimits m_JobLimits;
		};

		explicit FrameCaptureCoordinator(const CreateInfo& createInfo) noexcept;
		~FrameCaptureCoordinator();
		GGLAB_DELETE_COPYABLE_MOVABLE(FrameCaptureCoordinator);

		// Host-neutral opt-in seam; it is not yet a versioned control protocol.
		// Admission reserves a terminal receipt before moving the plan. Busy and
		// rejection allocate no storage and leave the supplied plan available to retry.
		[[nodiscard]] CaptureAdmission TrySubmit(CapturePlan&& plan) noexcept;
		// All methods run on the AppRuntime owner thread. Receipts do not expire:
		// consumption releases the reserved slot; polling an active/unknown job does not.
		[[nodiscard]] std::optional<CaptureJobResult> TakeJobResult(uint64_t jobId) noexcept;
		[[nodiscard]] std::optional<CaptureJobSnapshot> GetJobSnapshot(uint64_t jobId) const noexcept;
		[[nodiscard]] CaptureIntakeSnapshot GetIntakeSnapshot() const noexcept;
		// Issued cancellation remains a separate external-contract migration gate.
		[[nodiscard]] CaptureWaitingCancelResult CancelWaitingJob(uint64_t jobId) noexcept;

		// Returns the non-zero request id.
		[[nodiscard]] uint64_t Submit(FrameCaptureRequest request) noexcept;
		// Internal composition seam. One SameFrame plan owns all required products;
		// IDs/results remain per legacy output (resources and their graph share one).
		[[nodiscard]] std::vector<uint64_t> SubmitPlan(CapturePlan plan) noexcept;
		// Cancels an entirely waiting plan through any of its legacy output IDs.
		bool Cancel(uint64_t requestId) noexcept;
		// Appends finished results in completion order. When composed by
		// GGLabAppRuntime, only that owner drains this stream; hosts consume the
		// runtime's published results after it has notified the sequence.
		void ConsumeResults(std::vector<FrameCaptureRequestResult>& outResults) noexcept;
		[[nodiscard]] bool HasFrameResourceExports() const noexcept;
		[[nodiscard]] bool HasNonFiniteInstrumentation() const noexcept { return m_Capture && m_Capture->HasNonFiniteInstrumentation(); }
		[[nodiscard]] NonFiniteWatchCoordinator* GetNonFiniteWatch() const noexcept { return m_NonFiniteWatch; }
		[[nodiscard]] std::shared_ptr<const NonFiniteProductionFrame> GetLatestNonFiniteProductionFrame() const noexcept { return m_Capture ? m_Capture->GetLatestNonFiniteProductionFrame() : nullptr; }
		[[nodiscard]] uint32_t GetUnfinishedRequestCount() const noexcept;
		// Checks Runtime payload capacity and the write slots reserved by issued
		// captures, using the last frame's display target. Busy sequences defer before
		// simulation; permanent errors proceed to an explicit failed capture result.
		[[nodiscard]] FrameCaptureAvailability GetCaptureAvailability(FrameCaptureSource source, uint32_t requestCount = 1)
			const noexcept;
		[[nodiscard]] FrameCaptureAvailability GetCaptureAvailability(const CapturePlan& plan) const noexcept;
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
		// Called before the frame described by the state is built. Issues due
		// requests restricted to this frame; conflicting diagnostic taps wait
		// before metadata is fixed, or fail if they require an exact sequence frame.
		// Fails waiting after-ready requests for content with a failed gate.
		void BeginFrame(FrameCaptureFrameState state) noexcept;
		void SetFrameSceneInputs(uint64_t frameSerial, const FrameCaptureSceneInputs& inputs) noexcept;
		// Busy is handled before submission so a sequence keeps the same sample/frame.
		[[nodiscard]] bool PrepareResourceExports(uint64_t frameSerial, RenderGraph& graph,
			ResourceExportFrameContext context) noexcept;
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
		struct FrameAttempt;
		struct ProductAttempt;
		struct CaptureJob;
		struct CaptureOutput;
		struct JobReceipt;

		enum class Phase : uint8_t
		{
			Waiting,
			Issued,
			AwaitingWatch,
			Encoding,
			Finished,
		};

		// Transitional input adapter; accepted jobs retain only the typed plan.
		[[nodiscard]] static CapturePlan MakeCapturePlan(FrameCaptureRequest request) noexcept;
		[[nodiscard]] std::shared_ptr<CaptureJob> EnqueuePlan(CapturePlan plan,
			std::optional<size_t> receiptSlot = std::nullopt, uint64_t jobId = 0) noexcept;
		void RecordJobOutput(CaptureOutput& entry, const FrameCaptureRequestResult& result) noexcept;
		void CompleteJob(CaptureJob& job) noexcept;
		[[nodiscard]] static bool MatchesRequiredContent(
			const CaptureJob& job, const FrameCaptureFrameState& state) noexcept;
		[[nodiscard]] bool IsDue(
			const CaptureJob& job, const FrameCaptureFrameState& state) const noexcept;
		[[nodiscard]] FrameCaptureAvailability GetCaptureAvailability(FrameCaptureSource source,
			const FrameCaptureFrameState& state, uint32_t requestCount = 1) const noexcept;
		[[nodiscard]] FrameCaptureAvailability GetCaptureAvailability(
			const CapturePlan& plan, const FrameCaptureFrameState& state) const noexcept;
		[[nodiscard]] std::vector<CaptureOutput*> GetOutputs(const CaptureJob& job) noexcept;
		void IssueRequiredPlans(const FrameCaptureFrameState& state,
			std::optional<PostProcessDebugTap>& diagnosticTap) noexcept;
		void Issue(CaptureOutput& entry, const FrameCaptureFrameState& state,
			bool preAdmitted = false) noexcept;
		void FailPlan(CaptureJob& job, std::string failure) noexcept;
		void RetryPlan(CaptureJob& job, uint64_t consumedRequestId) noexcept;
		void HandleCaptureResult(FrameCaptureResult result) noexcept;
		void PollWatchCaptures() noexcept;
		void StartEncoding(CaptureOutput& entry, const FrameCaptureResult& result) noexcept;
		[[nodiscard]] static CaptureProductEncodeInput MakeEncodeInput(
			CaptureOutput& entry, const FrameCaptureResult& result) noexcept;
		void StartBundleEncoding(CaptureJob& job) noexcept;
		void FinishBundleWriting(CaptureJob& job, const CaptureWriteReceipt& receipt) noexcept;
		// Publishes the result of a written output; false while it is unfinished.
		[[nodiscard]] bool TryFinishWritten(CaptureOutput& entry) noexcept;
		void CollectWrittenJobs() noexcept;
		// Waits for unfinished writing until the shutdown write timeout, then
		// fails what remains and abandons jobs that have not claimed publication.
		void FinishWriting() noexcept;
		void Finish(CaptureOutput& entry, FrameCaptureRequestStatus status, std::string failure) noexcept;
		void RemoveFinishedJobs() noexcept;

		FrameCaptureControlBase* m_Capture = nullptr;
		NonFiniteWatchCoordinator* m_NonFiniteWatch = nullptr;
		std::filesystem::path m_DefaultOutputDirectory;
		std::chrono::milliseconds m_ShutdownWriteTimeout{ 30000 };
		std::unique_ptr<CaptureWriter> m_Writer;
		std::string m_RunIdentity;
		CaptureJobLimits m_JobLimits;
		// Fixed at construction. Each accepted job reserves one slot until consumed,
		// including while active. Rejected submissions never create failure receipts.
		std::vector<JobReceipt> m_JobReceipts;
		uint32_t m_ActiveJobs = 0;
		uint64_t m_NextJobId = 1;
		uint64_t m_NextRequestId = 1;
		std::vector<CaptureOutput> m_Outputs;
		std::vector<FrameCaptureRequestResult> m_Results;
		// Runtime retains and retires these payloads normally after a plan has failed
		// or retried; their late results must not reach a new attempt.
		std::vector<uint64_t> m_DiscardedCaptureResults;
		std::optional<FrameCaptureFrameState> m_LastFrameState;
		uint32_t m_SettledFrames = 0;
		// Every frame of the current settle key so far had every gate ready.
		bool m_HistoryReady = false;
		bool m_HasOpenFrame = false;
		// A partially issued plan must abort through the normal pre-submit path.
		std::optional<uint64_t> m_AbortFrameSerial;
		bool m_IsShuttingDown = false;
	};
}
