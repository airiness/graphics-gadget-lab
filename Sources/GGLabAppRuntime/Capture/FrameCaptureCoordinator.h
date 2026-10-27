#pragma once
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "Capture/CaptureEnvironment.h"
#include "Capture/CapturePlan.h"
#include "Capture/CaptureJobTypes.h"
#include "Capture/FrameCaptureImageEncoder.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/FrameCaptureReadiness.h"
#include "Parameters/HostViewParameters.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalHistoryTypes.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	class FrameCaptureControlBase;
	class GGLabAppRuntime;
	class NonFiniteWatchCoordinator;
	class CapturePublicationFileSystemBase;
	class CaptureWriter;
	struct CaptureProductEncodeInput;
	struct CaptureWriteReceipt;
	struct CaptureWriteJob;
	struct SequenceArtifactBundleInput;

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

	enum class FrameCaptureFrameOutcome : uint8_t
	{
		Planned,
		Submitted,
		Aborted,
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
		// Internal settling provenance; persisted parameter snapshots are a separate contract.
		uint64_t m_ParameterRevision = 0;
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
		HostViewControlStamp m_HostViewControl{};
		// Final display-view values, attributed through this frame's sequence/reference patches.
		std::vector<HostViewProjectionEntry> m_HostViewProjection;
		// Planned until the coordinator records submission or an abort.
		FrameCaptureFrameOutcome m_Outcome = FrameCaptureFrameOutcome::Planned;
	};

	// A camera reference view the runtime restores for a waiting job.
	struct FrameCaptureViewChange
	{
		uint64_t m_JobId = 0;
		std::string m_ReferenceViewId;
	};

	// Schedules required SameFrame products, then hands immutable CPU products to
	// a dedicated writer. Nine delivery reservations cover GPU issue through writing.
	// Every successful job publishes one bundle; every accepted job retains one receipt.
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
			// Recorded in every bundle manifest; unknown values stay null.
			CaptureEnvironment m_Environment;
			// The composed process run identity, shared with session control. Empty means
			// creation failed; standalone coordinators omit it and create their own.
			std::optional<std::string> m_RunIdentity;
		};

		explicit FrameCaptureCoordinator(const CreateInfo& createInfo) noexcept;
		~FrameCaptureCoordinator();
		GGLAB_DELETE_COPYABLE_MOVABLE(FrameCaptureCoordinator);

		// Host-neutral job admission; protocol frontends build this same typed plan.
		// Admission reserves a terminal receipt before moving the plan. Busy and
		// rejection allocate no storage and leave the supplied plan available to retry.
		[[nodiscard]] CaptureAdmission TrySubmit(CapturePlan&& plan) noexcept;
		// Advisory preflight with the same validation/pressure ordering. Accepted
		// reserves nothing and has no job ID; only TrySubmit creates an accepted job.
		[[nodiscard]] CaptureAdmission CheckJobAdmission(const CapturePlan& plan) const noexcept;
		[[nodiscard]] std::optional<std::chrono::steady_clock::time_point>
			GetNextFrameSelectionDeadline() const noexcept;
		// CPU-only expiry; an open issued attempt settles before its deadline is applied.
		void ExpireFrameSelections(
			std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept;
		// All methods run on the AppRuntime owner thread. Receipts do not expire:
		// consumption releases the reserved slot; polling an active/unknown job does not.
		// Standalone use only: a composed coordinator leaves consumption to AppRuntime.
		[[nodiscard]] std::optional<CaptureJobResult> TakeJobResult(uint64_t jobId) noexcept;
		[[nodiscard]] std::optional<CaptureJobSnapshot> GetJobSnapshot(uint64_t jobId) const noexcept;
		[[nodiscard]] CaptureIntakeSnapshot GetIntakeSnapshot() const noexcept;
		// Cancels the whole job's delivery, including issued work. Runtime/readback
		// and writer owners drain independently; a claimed publication is TooLate.
		[[nodiscard]] CaptureCancelResult CancelJob(uint64_t jobId) noexcept;
		[[nodiscard]] bool HasFrameResourceExports() const noexcept;
		// Issued deliveries from earlier frames may still be awaiting readback.
		[[nodiscard]] bool HasFrameGraphExports(uint64_t frameSerial) const noexcept;
		[[nodiscard]] bool HasNonFiniteInstrumentation() const noexcept { return m_Capture && m_Capture->HasNonFiniteInstrumentation(); }
		[[nodiscard]] NonFiniteWatchCoordinator* GetNonFiniteWatch() const noexcept { return m_NonFiniteWatch; }
		[[nodiscard]] std::shared_ptr<const NonFiniteProductionFrame> GetLatestNonFiniteProductionFrame() const noexcept { return m_Capture ? m_Capture->GetLatestNonFiniteProductionFrame() : nullptr; }
		[[nodiscard]] uint32_t GetUnfinishedRequestCount() const noexcept;
		// Checks Runtime payload capacity and the write slots reserved by issued
		// captures, using the last frame's display target. Busy sequences defer before
		// simulation; permanent errors proceed to an explicit failed capture result.
		[[nodiscard]] FrameCaptureAvailability GetCaptureAvailability(const CapturePlan& plan) const noexcept;
		// State of the most recent frame passed to BeginFrame; null before the
		// first rendered frame.
		[[nodiscard]] const FrameCaptureFrameState* GetLastFrameState() const noexcept;
		// The process environment record as JSON; empty only if it could not be serialized.
		[[nodiscard]] const std::string& GetEnvironmentJson() const noexcept { return m_EnvironmentJson; }
		// Consecutive submitted frames with every gate ready and an unchanged
		// settle key whose temporal history began on a ready frame.
		[[nodiscard]] uint32_t GetSettledFrameCount() const noexcept { return m_SettledFrames; }

		// Called before the next frame is planned, with the state of the previous
		// frame. Returns the reference view to restore on the active content's
		// main camera; the runtime reports the outcome to OnReferenceViewApplied.
		[[nodiscard]] std::optional<FrameCaptureViewChange> GetPendingViewChange() const noexcept;
		// A view that could not be restored fails its job.
		void OnReferenceViewApplied(uint64_t jobId, bool restored) noexcept;
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
		void SetFrameParameters(uint64_t frameSerial,
			std::shared_ptr<const ParameterFrameState> parameters) noexcept;
		// Busy is handled before submission so a sequence keeps the same sample/frame.
		[[nodiscard]] bool PrepareResourceExports(uint64_t frameSerial, RenderGraph& graph,
			ResourceExportFrameContext context) noexcept;
		// Called after the frame passed to BeginFrame was submitted.
		void OnFrameSubmitted() noexcept;
		// Called when the frame passed to BeginFrame ends before submission.
		void OnFrameAborted() noexcept;
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
		friend class GGLabAppRuntime;
		friend class FrameSequenceCoordinator;

		// Shares the writer and one of the same nine write reservations. Busy leaves
		// the immutable sequence product with its owner for a later poll.
		[[nodiscard]] std::shared_ptr<CaptureWriteJob> TryPublishSequenceArtifacts(
			const SequenceArtifactBundleInput& input, const std::filesystem::path& directory,
			std::string_view label, std::string& error) noexcept;
		[[nodiscard]] std::optional<CaptureWriteReceipt> GetSequenceArtifactReceipt(
			const CaptureWriteJob& job) const noexcept;
		[[nodiscard]] size_t GetWriteReservationCount() const noexcept;

		// Bound once during composition, before admission. Published receipts stay in
		// their original slots until the host consumes them through AppRuntime.
		void SetAppRuntimeJobResultOwner() noexcept;
		void PublishAppRuntimeJobResults() noexcept;
		[[nodiscard]] const CaptureJobResult* FindPublishedJobResult(uint64_t jobId) const noexcept;
		[[nodiscard]] std::optional<CaptureJobResult> TakePublishedJobResult(uint64_t jobId) noexcept;
		[[nodiscard]] std::optional<CaptureJobResult> ConsumeJobReceipt(uint64_t jobId) noexcept;

		struct FrameAttempt;
		struct ProductAttempt;
		struct CaptureJob;
		struct ProductDelivery;
		struct JobReceipt;
		struct DrainingWrite;

		enum class Phase : uint8_t
		{
			Waiting,
			Issued,
			AwaitingWatch,
			Encoding,
			Finished,
		};

		[[nodiscard]] std::shared_ptr<CaptureJob> EnqueuePlan(CapturePlan plan,
			size_t receiptSlot, uint64_t jobId) noexcept;
		void CompleteJob(CaptureJob& job) noexcept;
		[[nodiscard]] static bool MatchesRequiredContent(
			const CaptureJob& job, const FrameCaptureFrameState& state) noexcept;
		[[nodiscard]] bool IsDue(
			const CaptureJob& job, const FrameCaptureFrameState& state) const noexcept;
		[[nodiscard]] static RequiredFrameStateEvaluation EvaluateRequiredFrameState(
			const CaptureJob& job, const FrameCaptureFrameState& state);
		// Fails a due plan whose required frame state the frame contradicts.
		[[nodiscard]] bool FailRequiredFrameStateMismatch(
			CaptureJob& job, const FrameCaptureFrameState& state) noexcept;
		[[nodiscard]] FrameCaptureAvailability GetCaptureAvailability(FrameCaptureSource source,
			const FrameCaptureFrameState& state, uint32_t requestCount = 1) const noexcept;
		[[nodiscard]] FrameCaptureAvailability GetCaptureAvailability(
			const CapturePlan& plan, const FrameCaptureFrameState& state) const noexcept;
		[[nodiscard]] std::vector<ProductDelivery*> GetDeliveries(const CaptureJob& job) noexcept;
		void IssueRequiredPlans(const FrameCaptureFrameState& state,
			std::optional<PostProcessDebugTap>& diagnosticTap) noexcept;
		void Issue(ProductDelivery& entry, const FrameCaptureFrameState& state,
			bool preAdmitted = false) noexcept;
		void FailPlan(CaptureJob& job, std::string failure, std::string_view failureCode = {}) noexcept;
		[[nodiscard]] bool ExpireFrameSelection(CaptureJob& job,
			std::chrono::steady_clock::time_point now) noexcept;
		void RetryPlan(CaptureJob& job, uint64_t consumedRequestId) noexcept;
		void HandleCaptureResult(FrameCaptureResult result) noexcept;
		void PollWatchCaptures() noexcept;
		[[nodiscard]] static CaptureProductEncodeInput MakeEncodeInput(
			ProductDelivery& entry, const FrameCaptureResult& result) noexcept;
		void StartBundleEncoding(CaptureJob& job) noexcept;
		void FinishBundleWriting(CaptureJob& job, const CaptureWriteReceipt& receipt) noexcept;
		// Settles a written bundle; false while the writer is unfinished.
		[[nodiscard]] bool TryFinishWritten(ProductDelivery& entry) noexcept;
		void CollectWrittenJobs() noexcept;
		void CollectDrainingWrites() noexcept;
		// Waits for unfinished writing until the shutdown write timeout, then
		// fails what remains and abandons jobs that have not claimed publication.
		void FinishWriting() noexcept;
		void Finish(ProductDelivery& entry, CaptureJobStatus status, std::string failure) noexcept;
		void RemoveFinishedJobs() noexcept;

		FrameCaptureControlBase* m_Capture = nullptr;
		NonFiniteWatchCoordinator* m_NonFiniteWatch = nullptr;
		std::filesystem::path m_DefaultOutputDirectory;
		std::chrono::milliseconds m_ShutdownWriteTimeout{ 30000 };
		std::unique_ptr<CaptureWriter> m_Writer;
		std::shared_ptr<CaptureWriteJob> m_SequenceArtifactWrite;
		bool m_SequenceWriteHasTiming = false;
		std::shared_ptr<const CaptureWriteReceipt> m_SequenceArtifactShutdownReceipt;
		std::string m_RunIdentity;
		// Serialized once after the run identity it records.
		std::string m_EnvironmentJson;
		CaptureJobLimits m_JobLimits;
		// Fixed at construction. Each accepted job reserves one slot until consumed,
		// including while active. Rejected submissions never create failure receipts.
		std::vector<JobReceipt> m_JobReceipts;
		bool m_AppRuntimeOwnsJobResults = false;
		uint32_t m_ActiveJobs = 0;
		uint64_t m_NextJobId = 1;
		uint64_t m_NextArtifactRequestId = 1;
		std::vector<ProductDelivery> m_Deliveries;
		// Runtime retains and retires these payloads normally after a plan has failed
		// or retried; their late results must not reach a new attempt.
		std::vector<uint64_t> m_DiscardedCaptureResults;
		// Cancelled delivery never returns reservations still held by Runtime or a
		// writer. These drain records outlive consumption/reuse of the job receipt.
		std::vector<uint64_t> m_DrainingCaptureRequests;
		std::vector<DrainingWrite> m_DrainingWrites;
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
