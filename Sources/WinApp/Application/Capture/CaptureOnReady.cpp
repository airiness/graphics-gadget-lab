#include "Application/Capture/CaptureOnReady.h"

#include <utility>

namespace gglab
{
	CaptureOnReady::CaptureOnReady(CreateInfo createInfo) noexcept :
		m_Info(std::move(createInfo))
	{
	}

	void CaptureOnReady::TryAdmit() noexcept
	{
		if (m_State != State::Pending || GetJobId() != 0)
		{
			return;
		}
		m_Admission = m_Info.m_TrySubmit(std::move(m_Info.m_Plan));
		if (m_Admission.m_Status == CaptureAdmissionStatus::Rejected)
		{
			m_State = State::Rejected;
			m_Info.m_Plan = {};
		}
		else if (m_Admission.m_Status == CaptureAdmissionStatus::Accepted)
		{
			m_Info.m_Plan = {};
		}
	}

	void CaptureOnReady::CollectResult() noexcept
	{
		if (!m_Result && GetJobId() != 0)
		{
			m_Result = m_Info.m_TakeResult(GetJobId());
		}
	}

	CaptureOnReady::State CaptureOnReady::Poll(std::chrono::steady_clock::time_point now) noexcept
	{
		if (m_State != State::Pending)
		{
			return m_State;
		}
		if (!m_Deadline)
		{
			m_Deadline = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(m_Info.m_Timeout);
		}
		CollectResult();
		if (m_Result)
		{
			m_State = m_Result->m_Status == CaptureJobStatus::Cancelled ?
				State::Cancelled :
				m_Result->m_Status == CaptureJobStatus::Completed &&
				m_Result->m_Publication == CaptureJobPublication::Published ?
					State::Completed :
					State::Failed;
		}
		else if (now >= *m_Deadline)
		{
			if (GetJobId() != 0)
			{
				m_Cancellation = m_Info.m_Cancel(GetJobId());
			}
			m_Info.m_Plan = {};
			m_State = State::TimedOut;
		}
		return m_State;
	}
}
