#include "Application/Control/ApplicationControlCaptureJobs.h"
#include "Application/Control/ApplicationControlProtocol.h"
#include "GGLabAppRuntime.h"

#include <algorithm>
#include <utility>

namespace gglab
{
	ApplicationControlCaptureJobs::ApplicationControlCaptureJobs(CreateInfo createInfo) noexcept :
		m_Info(std::move(createInfo))
	{
		GGLAB_ASSERT(m_Info.m_TrySubmit && m_Info.m_Cancel && m_Info.m_FindResult &&
			m_Info.m_TakeResult && m_Info.m_GetSnapshot);
	}

	ApplicationControlCaptureJobs::ApplicationControlCaptureJobs(GGLabAppRuntime& runtime) noexcept :
		ApplicationControlCaptureJobs(CreateInfo{
			.m_TrySubmit = [&runtime](CapturePlan&& plan)
			{
				return runtime.TrySubmitCaptureJob(std::move(plan));
			},
			.m_Cancel = [&runtime](uint64_t jobId) { return runtime.CancelCaptureJob(jobId); },
			.m_FindResult = [&runtime](uint64_t jobId) { return runtime.FindCaptureJobResult(jobId); },
			.m_TakeResult = [&runtime](uint64_t jobId) { return runtime.TakeCaptureJobResult(jobId); },
			.m_GetSnapshot = [&runtime](uint64_t jobId) { return runtime.GetCaptureJobSnapshot(jobId); },
			})
	{
	}

	std::string ApplicationControlCaptureJobs::Submit(uint64_t id, CapturePlan&& plan) noexcept
	{
		const CaptureAdmission admission = m_Info.m_TrySubmit(std::move(plan));
		if (admission.m_Status == CaptureAdmissionStatus::Accepted)
		{
			m_Jobs.push_back(admission.m_JobId);
		}
		return SerializeApplicationControlCaptureAdmission(id, admission);
	}

	std::string ApplicationControlCaptureJobs::Query(
		uint64_t id, uint64_t jobId, bool consume) noexcept
	{
		const auto owned = std::ranges::find(m_Jobs, jobId);
		if (owned == m_Jobs.end())
		{
			return SerializeApplicationControlCaptureNotFound(id, jobId);
		}
		if (const CaptureJobResult* result = m_Info.m_FindResult(jobId))
		{
			// Serialize while borrowed. Consume only after the complete response exists;
			// a disconnected client can still lose a response, but cannot duplicate a receipt.
			std::string response = SerializeApplicationControlCaptureResult(id, *result, consume);
			if (consume && m_Info.m_TakeResult(jobId))
			{
				m_Jobs.erase(owned);
			}
			return response;
		}
		if (const std::optional<CaptureJobSnapshot> snapshot = m_Info.m_GetSnapshot(jobId))
		{
			return SerializeApplicationControlCapturePending(id, *snapshot);
		}
		// AppRuntime retains no receipt for this identity, so there is nothing left to own.
		m_Jobs.erase(owned);
		return SerializeApplicationControlCaptureNotFound(id, jobId);
	}

	std::string ApplicationControlCaptureJobs::Cancel(uint64_t id, uint64_t jobId) noexcept
	{
		// Cancellation keeps the identity: the terminal receipt still needs consumption.
		const bool owned = std::ranges::find(m_Jobs, jobId) != m_Jobs.end();
		return SerializeApplicationControlCaptureCancellation(id, jobId,
			owned ? m_Info.m_Cancel(jobId) : CaptureCancelResult::NotFound);
	}
}
