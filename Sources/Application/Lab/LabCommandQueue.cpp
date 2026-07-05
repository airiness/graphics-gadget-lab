#include "Core/Precompiled.h"
#include "Application/Lab/LabCommandQueue.h"

namespace gglab
{
	void LabCommandQueue::RequestSwitch(const LabId& id) noexcept
	{
		if (id.IsValid())
		{
			m_Pending.m_SwitchTarget = id;
		}
	}

	void LabCommandQueue::RequestRestart() noexcept
	{
		m_Pending.m_RestartRequested = true;
	}

	LabCommandBatch LabCommandQueue::Consume() noexcept
	{
		LabCommandBatch result = std::move(m_Pending);
		m_Pending = {};
		return result;
	}

	bool LabCommandQueue::IsEmpty() const noexcept
	{
		return !m_Pending.m_SwitchTarget.has_value() && !m_Pending.m_RestartRequested;
	}
}
