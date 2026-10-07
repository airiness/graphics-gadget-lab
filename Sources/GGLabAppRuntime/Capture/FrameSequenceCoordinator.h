#pragma once
#include "Capture/FrameCaptureCoordinator.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/CameraPath.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	struct FrameSequenceRequest
	{
		// Camera path registered by the active content.
		std::string m_CameraPathId;
		// When set, the sequence also waits until the active Demo id or Lab id equals it.
		std::string m_RequiredContentId;
		// Sequence frames captured on exactly that frame; each must be below the
		// path's frame count. Duplicates are ignored.
		std::vector<uint32_t> m_CaptureFrames;
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
	};

	enum class FrameSequenceState : uint8_t
	{
		// Waiting for ready content before frame 0.
		Waiting,
		Running,
		// Every frame was submitted; captures are still being written.
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
		std::vector<uint64_t> m_CaptureRequestIds;
		uint32_t m_CompletedCaptures = 0;
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
		// render a frame, so the sequence stays deterministic; the writer drains on
		// its own thread.
		[[nodiscard]] bool ShouldDeferFrame() const noexcept;
		// True for every reference sample after the first of a sequence frame: the
		// runtime then starts the frame without advancing simulation time.
		[[nodiscard]] bool ShouldHoldTime() const noexcept;

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
		void BeginFrame(const FrameCaptureFrameState& state) noexcept;
		// Called after the frame passed to BeginFrame was submitted.
		void OnFrameSubmitted() noexcept;
		// Counts finished captures of the sequence; a failed capture fails it.
		void OnCaptureResults(std::span<const FrameCaptureRequestResult> results) noexcept;

	private:
		void Fail(std::string failure) noexcept;
		void CompleteIfFinished() noexcept;
		// The posed frame is a requested frame, at its last reference sample, and has
		// not been captured yet.
		[[nodiscard]] bool IsCaptureDue() const noexcept;

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
		std::optional<uint32_t> m_CapturedFrame;
		std::optional<FrameCaptureSettleKey> m_LastSettleKey;
	};
}
