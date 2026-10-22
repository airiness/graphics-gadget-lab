#include "Lab/LabRuntime.h"
#include "GGLabRuntime/Graphics/Camera.h"

#include <limits>

namespace gglab
{
	bool LabRuntime::SupportsParameterEffect(const LabParameterDesc& desc) const noexcept
	{
		const auto effect = GetParameterEffectClass(desc);
		return effect == ParameterEffectClass::Immediate ||
			(effect == ParameterEffectClass::Deferred && m_ActiveSession &&
				m_ActiveSession->GetLightingParameterRequest(desc.m_Id).has_value());
	}

	bool LabRuntime::QueueParameterBatch(uint64_t id,
		std::span<const LabParameterValue> values, std::string& error) noexcept
	{
		if (!m_CommandQueue.IsEmpty() || !m_IsEntered || !IsReady() || m_PendingSession ||
			m_ParameterRevision == std::numeric_limits<uint64_t>::max())
		{
			error = "Busy: a Lab batch requires a ready active Lab and no pending commands.";
			return false;
		}
		auto candidate = m_ActiveSession->GetParameters();
		if (!candidate.ApplyBatch(values, error))
		{
			return false;
		}
		size_t lightingItems = 0;
		for (const auto& value : values)
		{
			const auto& desc = candidate.Find(value.m_Id)->m_Desc;
			lightingItems += GetParameterEffectClass(desc) == ParameterEffectClass::Deferred;
			if (!SupportsParameterEffect(desc) || lightingItems > 1)
			{
				error = "Unsupported effect: at most one bound Deferred lighting item is supported; rebuild effects are unsupported.";
				return false;
			}
		}
		m_CommandQueue.RequestParameterBatch(id);
		return true;
	}

	void LabRuntime::CancelParameterBatch(uint64_t id) noexcept
	{
		m_CommandQueue.CancelParameterBatch(id);
	}

	bool LabRuntime::ApplyParameterBatch(uint64_t id,
		std::span<const LabParameterValue> values, std::string& error) noexcept
	{
		if (!m_CommandQueue.ConsumeParameterBatch(id) || !m_IsEntered || !IsReady() || m_PendingSession ||
			m_ParameterRevision == std::numeric_limits<uint64_t>::max())
		{
			error = "The Lab parameter command lost its ready owning session.";
			return false;
		}
		const auto applied = m_ActiveSession->SetParameters(values, error);
		if (!applied)
		{
			return false;
		}
		if (!applied->m_Changed.empty())
		{
			++m_ParameterRevision;
			m_ActiveSession->ApplyParameterChanges(LabChangeImpact::Immediate);
			m_ActiveSession->GetCamera().Update();
		}
		return true;
	}
}
