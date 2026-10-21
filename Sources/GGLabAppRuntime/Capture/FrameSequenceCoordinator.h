#pragma once
#include "Capture/FrameCaptureCoordinator.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/CameraPath.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfileFrameSnapshot.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	// Display-view Temporal AA settings a sequence evaluates instead of the content's.
	// Unset fields keep the content's resolved values.
	struct FrameSequenceTemporalAAOverrides
	{
		// False evaluates the same sequence with Temporal AA not requested.
		std::optional<bool> m_Enabled;
		std::optional<float> m_MaxHistoryFeedback;
		std::optional<float> m_DepthAbsoluteThreshold;
		std::optional<float> m_DepthRelativeThreshold;
		std::optional<float> m_VelocityWeightScale;
		std::optional<float> m_LuminanceWeightScale;
		std::optional<float> m_NeighborhoodClampExpansion;
		std::optional<float> m_HistoryRelaxation;
		std::optional<TemporalAAHistoryAccumulation> m_HistoryAccumulation;
		std::optional<TemporalAAHistoryRectification> m_HistoryRectification;
		std::optional<float> m_VarianceClipGamma;
		std::optional<TemporalAAHistoryFilter> m_HistoryFilter;
		std::optional<TemporalAACurrentFilter> m_CurrentFilter;
		std::optional<TemporalAAMotionSelection> m_MotionSelection;
		std::optional<TemporalAAPostTemporalView> m_PostTemporalView;
		std::optional<TemporalAAResolutionPreset> m_ResolutionPreset;
		std::optional<float> m_TextureLodBiasOffset;

		[[nodiscard]] bool IsEmpty() const noexcept;
	};

	// Returns the settings with every set override applied and resolved to the
	// settings' valid ranges.
	[[nodiscard]] TemporalAASettings ApplyFrameSequenceTemporalAAOverrides(
		const FrameSequenceTemporalAAOverrides& overrides,
		TemporalAASettings settings) noexcept;

	struct FrameSequenceGTAOOverrides
	{
		// True evaluates the same sequence with GTAO accumulated over submitted frames.
		std::optional<bool> m_TemporalAccumulation;
		std::optional<uint32_t> m_TemporalMaxSamples;
		std::optional<uint32_t> m_DirectionCount;
		std::optional<uint32_t> m_StepCount;

		[[nodiscard]] bool IsEmpty() const noexcept
		{
			return !m_TemporalAccumulation && !m_TemporalMaxSamples && !m_DirectionCount &&
				!m_StepCount;
		}
	};

	// Returns the GTAO settings with every set override applied and resolved to the
	// settings' valid ranges.
	[[nodiscard]] GTAOSettings ApplyFrameSequenceGTAOOverrides(
		const FrameSequenceGTAOOverrides& overrides, GTAOSettings settings) noexcept;

	struct FrameSequenceResourceExport
	{
		uint32_t m_Frame = 0;
		std::vector<RGResourceAddress> m_Targets;
		bool m_IncludeGraph = false;
	};

	struct FrameSequenceRequest
	{
		// Camera path registered by the active content.
		std::string m_CameraPathId;
		// When set, the sequence also waits until the active Demo id or Lab id equals it.
		std::string m_RequiredContentId;
		// Sequence frames captured on exactly that frame; each must be below the
		// path's frame count. Duplicates are ignored.
		std::vector<uint32_t> m_CaptureFrames;
		std::vector<FrameSequenceResourceExport> m_Exports;
		FrameCaptureSource m_CaptureSource = FrameCaptureSource::Scene;
		// Required when the capture source is Diagnostic: the one evidence channel
		// the run records.
		std::optional<PostProcessDebugTap> m_DiagnosticTap;
		// Empty selects the capture coordinator's default output directory.
		std::filesystem::path m_OutputDirectory;
		// File subject prefix; empty uses the camera path id. Each capture appends
		// "-f<frame>" with four or more digits.
		std::string m_Label;
		std::string m_Note;
		// Non-zero renders every sequence frame as a supersampled reference of this many
		// jittered samples, with Temporal AA inactive and simulation time held; a
		// capture records the mean after the last sample.
		uint32_t m_ReferenceSamples = 0;
		// Material texture LOD bias of every reference sample; only valid for a reference.
		float m_ReferenceTextureLodBias = 0.0f;
		// Applied to every sequence frame. Frame 0 resets temporal history, so the run
		// evaluates one configuration from its first frame. Not valid for a reference.
		FrameSequenceTemporalAAOverrides m_TemporalAAOverrides;
		// Applied to every sequence frame like the Temporal AA overrides; not valid for a
		// reference.
		FrameSequenceGTAOOverrides m_GTAOOverrides;
		// Records the GPU timing of sequence frames while the sequence runs.
		bool m_GpuTiming = false;
	};

	// GPU times of one profiler scope over the timed frames that recorded it.
	struct FrameSequenceGpuTimingSeries
	{
		std::string m_Name;
		std::vector<double> m_Milliseconds;
		GpuProfileScopeKind m_Kind = GpuProfileScopeKind::Other;
		uint32_t m_Occurrence = 0;
	};

	struct FrameSequenceGpuTiming
	{
		// Whole-frame GPU time of every submitted sequence render, including each
		// reference sample. Includes cold/cut frames; no latency-based warmup skip.
		std::vector<double> m_FrameMilliseconds;
		// Scopes in first-recorded order.
		std::vector<FrameSequenceGpuTimingSeries> m_Scopes;
	};

	struct FrameSequenceTimingSummary
	{
		uint32_t m_Count = 0;
		double m_Mean = 0.0;
		double m_Median = 0.0;
		double m_P90 = 0.0;
		double m_Min = 0.0;
		double m_Max = 0.0;
	};

	// Nearest-rank percentiles; an empty input yields a zero count.
	[[nodiscard]] FrameSequenceTimingSummary SummarizeFrameSequenceTiming(
		std::span<const double> milliseconds);

	enum class FrameSequenceState : uint8_t
	{
		// Waiting for ready content before frame 0.
		Waiting,
		Running,
		// Every frame was submitted; captures and/or GPU profiles are still pending.
		Finishing,
		Completed,
		Failed,
		Cancelled,
	};

	[[nodiscard]] const char* GetFrameSequenceStateName(FrameSequenceState state) noexcept;

	struct FrameSequenceStatus
	{
		uint64_t m_SequenceId = 0;
		FrameSequenceState m_State = FrameSequenceState::Waiting;
		std::string m_CameraPathId;
		// Known once the sequence started.
		uint32_t m_CameraPathVersion = 0;
		uint32_t m_FrameCount = 0;
		// Samples per sequence frame of a reference sequence; zero otherwise.
		uint32_t m_ReferenceSamples = 0;
		// Sequence frames completed so far: every sample of a reference frame submitted.
		uint32_t m_SubmittedFrames = 0;
		uint32_t m_AcceptedCaptureJobs = 0;
		uint32_t m_FinishedCaptureJobs = 0;
		uint32_t m_PendingCaptureJobs = 0;
		uint32_t m_CompletedCaptures = 0;
		// Set when the request records GPU timing.
		std::optional<FrameSequenceGpuTiming> m_GpuTiming;
		std::shared_ptr<const NonFiniteWatchCoverage> m_WatchCoverage;
		std::string m_Failure;

		[[nodiscard]] bool IsTerminal() const noexcept
		{
			return m_State == FrameSequenceState::Completed ||
				m_State == FrameSequenceState::Failed ||
				m_State == FrameSequenceState::Cancelled;
		}
	};

	// Pose the runtime applies to the active content's main camera for this frame.
	struct FrameSequencePoseRequest
	{
		std::string m_CameraPathId;
		uint32_t m_Frame = 0;
		// Set for every sample of a reference sequence.
		std::optional<TemporalReferenceSample> m_ReferenceSample;
		FrameSequenceTemporalAAOverrides m_TemporalAAOverrides;
		FrameSequenceGTAOOverrides m_GTAOOverrides;
	};

	// Drives one camera-path sequence at a time. Frame 0 starts once every readiness
	// gate of the previous frame is ready; each submitted frame then advances the
	// path by exactly one frame, independent of wall-clock and simulation time.
	// Frame 0 and cut keys are camera cuts that reset temporal history, so a replay
	// starts from the same temporal state. Any other change of temporal continuity
	// (temporal session, display view, size or content) or a readiness gate leaving
	// Ready fails the sequence, because the remaining frames would no longer be
	// comparable. Requested frames are captured through the capture coordinator as
	// next-frame requests issued on exactly that frame. When the capture writer
	// cannot accept another capture, the runtime defers the next frame entirely
	// instead of letting the capture fail or rendering an extra frame.
	class FrameSequenceCoordinator final
	{
	public:
		explicit FrameSequenceCoordinator(FrameCaptureCoordinator& capture) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(FrameSequenceCoordinator);
		~FrameSequenceCoordinator() = default;

		// Returns the non-zero sequence id, or zero with an error when another
		// sequence is active or the request is incomplete.
		[[nodiscard]] uint64_t Start(FrameSequenceRequest request, std::string& outError) noexcept;
		// Cancels the active sequence; returns false when none is active.
		bool Cancel() noexcept;
		// Status of the active or most recent sequence.
		[[nodiscard]] const FrameSequenceStatus* GetStatus() const noexcept;
		[[nodiscard]] bool IsActive() const noexcept;
		// True while the next sequence frame needs a capture that the capture
		// coordinator cannot accept yet. The runtime must then neither simulate nor
		// render a frame, so the sequence stays deterministic. Poll capture results
		// to retire GPU work while the writer drains on its own thread.
		[[nodiscard]] bool ShouldDeferFrame() const noexcept;
		// True for later reference samples and retries of a begun, unsubmitted
		// sequence frame: the runtime must preserve the same simulation instant.
		[[nodiscard]] bool ShouldHoldTime() const noexcept;
		// True while an active sequence records GPU timing; the runtime then keeps GPU
		// profiling enabled.
		[[nodiscard]] bool WantsGpuTiming() const noexcept;

		// Called before the frame is planned with the capture state of the previous
		// frame (null before the first frame) and the active content's camera paths.
		// Starts a waiting sequence when that frame was ready, and returns the pose
		// to apply to this frame.
		[[nodiscard]] std::optional<FrameSequencePoseRequest> PrepareFrame(
			const FrameCaptureFrameState* previousFrame,
			std::span<const CameraPath> cameraPaths) noexcept;
		// Reports the pose returned by CameraRig::ApplyCameraPathFrame; an empty pose
		// fails the sequence.
		void OnPoseApplied(const std::optional<CameraPathPose>& pose) noexcept;
		// Called with the state of the posed frame before the capture coordinator's
		// BeginFrame: checks readiness and temporal continuity, then submits this
		// frame's capture when requested.
		// False aborts this frame before rendering when admission must retry or fails.
		[[nodiscard]] bool BeginFrame(const FrameCaptureFrameState& state) noexcept;
		// Called for every successful runtime submission, including ordinary frames
		// that publish remaining profiles while the sequence is Finishing.
		void OnFrameSubmitted(uint64_t renderFrameSerial) noexcept;
		// Polled independently of sequence posing and host status queries. Only a
		// profile bound to a pending successful sequence submission is accepted.
		void UpdateGpuTiming(const GpuProfileFrameSnapshot& profile, bool profilingEnabled) noexcept;

		// Liveness budget, not a frames-in-flight assumption or warmup policy.
		// Deferred/aborted frames consume no budget. Missing results fail explicitly.
		static constexpr uint32_t MaxPendingGpuTimingSubmissions = 64;
		// The AppRuntime owner consumes each sequence receipt directly from its reserved
		// slot. Terminal sequences continue draining; no result history is retained here.
		void UpdateCaptureJobs(
			const std::function<std::optional<CaptureJobResult>(uint64_t)>& takeResult) noexcept;
		void UpdateWatchCoverage() noexcept;

	private:
		struct PendingCaptureJob
		{
			uint64_t m_JobId = 0;
			uint32_t m_Frame = 0;
			uint32_t m_Deliveries = 0;
		};

		struct PendingGpuTimingFrame
		{
			uint64_t m_RenderFrameSerial = 0;
			uint64_t m_SubmissionOrdinal = 0;
			uint32_t m_SequenceFrame = 0;
			uint32_t m_ReferenceSample = 0;
		};

		void Fail(std::string failure) noexcept;
		void CancelCaptureJobs() noexcept;
		void CompleteIfFinished() noexcept;
		// The posed frame is a requested frame, at its last reference sample, and has
		// not been captured yet.
		[[nodiscard]] bool IsCaptureDue() const noexcept;
		[[nodiscard]] const FrameSequenceResourceExport* GetDueExport() const noexcept;
		[[nodiscard]] CapturePlan MakeFrameCapturePlan() const noexcept;

		FrameCaptureCoordinator* m_Capture = nullptr;
		uint64_t m_NextSequenceId = 1;
		std::optional<FrameSequenceStatus> m_Status;
		FrameSequenceRequest m_Request{};
		// Frame posed for the next submission; it is posed again if a frame ends
		// without submission.
		uint32_t m_Frame = 0;
		// Reference sample of the posed frame; always zero outside a reference sequence.
		uint32_t m_Sample = 0;
		std::optional<CameraPathPose> m_AppliedPose;
		bool m_FrameBegun = false;
		uint64_t m_BegunRenderFrameSerial = 0;
		std::optional<uint32_t> m_CapturedFrame;
		std::optional<uint32_t> m_ExportedFrame;
		std::vector<PendingCaptureJob> m_CaptureJobs;
		bool m_AdmissionDeferred = false;
		std::optional<FrameCaptureSettleKey> m_LastSettleKey;
		std::vector<PendingGpuTimingFrame> m_PendingGpuTimingFrames;
		uint64_t m_GpuTimingSubmissionCount = 0;
		uint64_t m_LastTimingSubmissionSerial = 0;
		uint64_t m_WatchCoverageId = 0;
		bool m_WatchStarted = false;
	};
}
