#include "Lab/LabCommandQueue.h"

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

	void LabCommandQueue::RequestSetParameter(
		const LabParameterId& id, const LabValue& value) noexcept
	{
		if (!id.IsValid())
		{
			return;
		}

		const auto iter = std::ranges::find_if(m_Pending.m_ParameterChanges,
			[&id](const LabParameterValue& change) { return change.m_Id == id; });
		if (iter != m_Pending.m_ParameterChanges.end())
		{
			iter->m_Value = value;
		}
		else
		{
			m_Pending.m_ParameterChanges.push_back({
				.m_Id = id,
				.m_Value = value,
				});
		}
	}

	void LabCommandQueue::RequestResetParameters() noexcept
	{
		m_Pending.m_ParameterChanges.clear();
		m_Pending.m_ResetParametersRequested = true;
	}

	void LabCommandQueue::RequestRebuildScene() noexcept
	{
		m_Pending.m_RebuildSceneRequested = true;
	}

	void LabCommandQueue::RequestRunConfig(const LabRunConfig& config) noexcept
	{
		m_Pending.m_RunConfig = config;
	}

	LabCommandBatch LabCommandQueue::Consume() noexcept
	{
		LabCommandBatch result = std::move(m_Pending);
		m_Pending = {};
		return result;
	}

	bool LabCommandQueue::IsEmpty() const noexcept
	{
		return !m_Pending.m_SwitchTarget.has_value() && !m_Pending.m_RunConfig.has_value() &&
			m_Pending.m_ParameterChanges.empty() && !m_Pending.m_RestartRequested &&
			!m_Pending.m_ResetParametersRequested && !m_Pending.m_RebuildSceneRequested;
	}
}
