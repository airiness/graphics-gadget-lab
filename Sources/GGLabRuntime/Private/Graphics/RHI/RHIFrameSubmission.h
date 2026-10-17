#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"

#include <cstdint>
#include <functional>
#include <span>

namespace gglab
{
	class RHIUploadBatch;

	// Bound for the waits a frame transaction uses to verify an uncertain submission.
	inline constexpr uint32_t RHIFrameVerificationTimeoutMilliseconds = 10'000;

	enum class RHISubmitEffect : uint8_t
	{
		// The stream failed before its native submit, or the native API guarantees that the
		// failed submit had no effect.
		NoEffect,
		// Every other outcome once the native submit was issued. A missing fence never proves
		// that an accepted stream did not execute.
		Accepted,
	};

	struct RHISubmittedStream
	{
		RHISubmitEffect m_Effect = RHISubmitEffect::NoEffect;
		// Valid only when the checked signal after the submit succeeded.
		RHIFencePoint m_Fence{};
		// A later stream's queue wait on m_Fence was confirmed.
		bool m_JoinedByNextStream = false;
	};

	struct RHISubmissionVerification
	{
		// Checked CPU wait on an earlier stream whose fence no later stream joined.
		std::function<RHIGpuWaitResult(const RHIFencePoint&)> m_WaitForFence;
		// Verified idle over every queue that may have consumed the frame. It must not
		// depend on a failed fence; Completed also releases quarantined pages.
		std::function<RHIGpuWaitResult()> m_WaitIdle;
	};

	struct RHISubmissionResolution
	{
		RHIFrameSubmissionState m_State = RHIFrameSubmissionState::NoOutstandingWork;
		// Valid only for Fenced: it completes after every accepted stream.
		RHIFencePoint m_Fence{};
		// A verification wait reported confirmed device loss.
		bool m_DeviceLost = false;
	};

	// Submits a dependent stream only after its prerequisite has a checked fence and a
	// confirmed queue join. A failed join leaves the dependent stream without effect.
	[[nodiscard]] RHISubmittedStream SubmitDependentFrameStream(
		RHISubmittedStream& prerequisite,
		const std::function<bool(const RHIFencePoint&)>& join,
		const std::function<RHISubmittedStream()>& submit) noexcept;

	// Settles the upload batch of one frame transaction from its ordered stream outcomes:
	// cancel when nothing was accepted, retire on a fence that covers every accepted stream,
	// or quarantine and try to verify idle. Shared by every backend frame transaction.
	[[nodiscard]] RHISubmissionResolution ResolveFrameSubmission(
		std::span<const RHISubmittedStream> streams, RHIUploadBatch& batch,
		const RHISubmissionVerification& verification) noexcept;
}
