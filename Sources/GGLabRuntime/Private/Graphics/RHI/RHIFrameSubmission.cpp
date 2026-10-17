#include "Graphics/RHI/RHIFrameSubmission.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIConstantUpload.h"

#include <vector>

namespace gglab
{
	namespace
	{
		// Every accepted stream before the last one must be covered by the last fence: either
		// a later stream's queue wait on it was confirmed, or a checked CPU wait completed.
		[[nodiscard]] bool AreEarlierStreamsCovered(
			std::span<const RHISubmittedStream* const> accepted,
			const RHISubmissionVerification& verification, bool& outDeviceLost) noexcept
		{
			for (size_t index = 0; index + 1 < accepted.size(); ++index)
			{
				const RHISubmittedStream& stream = *accepted[index];
				if (stream.m_JoinedByNextStream && stream.m_Fence.IsValid())
				{
					continue;
				}
				if (!stream.m_Fence.IsValid() || !verification.m_WaitForFence)
				{
					return false;
				}
				const RHIGpuWaitResult result = verification.m_WaitForFence(stream.m_Fence);
				outDeviceLost = outDeviceLost || result == RHIGpuWaitResult::DeviceLost;
				if (result != RHIGpuWaitResult::Completed)
				{
					return false;
				}
			}
			return true;
		}
	}

	RHISubmissionResolution ResolveFrameSubmission(std::span<const RHISubmittedStream> streams,
		RHIConstantUploadBatch& batch, const RHISubmissionVerification& verification) noexcept
	{
		std::vector<const RHISubmittedStream*> accepted;
		accepted.reserve(streams.size());
		for (const RHISubmittedStream& stream : streams)
		{
			if (stream.m_Effect == RHISubmitEffect::Accepted)
			{
				accepted.push_back(&stream);
			}
		}
		if (accepted.empty())
		{
			batch.Cancel();
			return {};
		}

		batch.RecordAcceptedSubmission();
		RHISubmissionResolution resolution{};
		const RHIFencePoint& lastFence = accepted.back()->m_Fence;
		if (lastFence.IsValid() &&
			AreEarlierStreamsCovered(accepted, verification, resolution.m_DeviceLost))
		{
			const bool submitted = batch.Submitted(lastFence);
			GGLAB_ASSERT_MSG(
				submitted, "A fenced submission must retain its constant upload pages.");
			if (submitted)
			{
				resolution.m_State = RHIFrameSubmissionState::Fenced;
				resolution.m_Fence = lastFence;
				return resolution;
			}
		}

		// Work that may read the pages has no reliable completion fence. Quarantine the pages
		// and release them only once every consuming queue is verified idle.
		batch.SubmittedWithoutCompletionFence();
		const RHIGpuWaitResult idle = verification.m_WaitIdle ?
			verification.m_WaitIdle() :
			RHIGpuWaitResult::Failed;
		resolution.m_DeviceLost = resolution.m_DeviceLost || idle == RHIGpuWaitResult::DeviceLost;
		if (idle == RHIGpuWaitResult::Completed)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"A frame submission had no reliable completion fence; verified idle released it.");
			resolution.m_State = RHIFrameSubmissionState::NoOutstandingWork;
			return resolution;
		}
		GGLAB_LOG_GRAPHICS_ERROR(
			"A frame submission had no reliable completion fence and idle could not be verified.");
		resolution.m_State = RHIFrameSubmissionState::CompletionUnknown;
		return resolution;
	}
}
