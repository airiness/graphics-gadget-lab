#include "Lab/LabRuntime.h"
#include "GGLabRuntime/Graphics/Camera.h"

#include <limits>
#include <utility>

namespace gglab
{
	bool LabRuntime::SupportsParameterEffect(const LabParameterDesc& desc) const noexcept
	{
		const auto effect = GetParameterEffectClass(desc);
		return (effect == ParameterEffectClass::Immediate && m_ActiveSession &&
			m_ActiveSession->SupportsImmediateParameter(desc.m_Id)) ||
			(effect == ParameterEffectClass::Deferred && m_ActiveSession &&
				m_ActiveSession->SupportsDeferredLightingParameter(desc.m_Id));
	}

	std::optional<LabRuntime::PreparedParameterBatch> LabRuntime::PrepareParameterValues(
		std::span<const LabParameterValue> values, std::string& error) const noexcept
	{
		if (!m_IsEntered || !IsReady() || m_PendingSession ||
			m_ParameterRevision == std::numeric_limits<uint64_t>::max())
		{
			error = "A Lab batch requires a ready entered owning session and available revision capacity.";
			return std::nullopt;
		}
		PreparedParameterBatch prepared;
		prepared.m_Owner = this;
		prepared.m_Session = m_ActiveSession.get();
		prepared.m_Revision = m_ParameterRevision;
		prepared.m_SessionSerial = m_TemporalSessionSerial;
		prepared.m_Parameters = m_ActiveSession->GetParameters();
		const auto applied = prepared.m_Parameters.ApplyBatch(values, error);
		if (!applied)
		{
			return std::nullopt;
		}
		size_t lightingItems = 0;
		for (const auto& value : values)
		{
			const auto& desc = prepared.m_Parameters.Find(value.m_Id)->m_Desc;
			if (GetParameterEffectClass(desc) == ParameterEffectClass::Deferred)
			{
				++lightingItems;
				prepared.m_LightingParameter = value.m_Id;
			}
			if (!SupportsParameterEffect(desc) || lightingItems > 1)
			{
				error = "Unsupported effect: Immediate items require a frame projection; "
					"at most one bound Deferred lighting item is supported; rebuild effects are unsupported.";
				return std::nullopt;
			}
		}
		prepared.m_HasChanges = !applied->m_Changed.empty();
		return prepared;
	}

	bool LabRuntime::QueueParameterBatch(uint64_t id,
		std::span<const LabParameterValue> values, std::string& error) noexcept
	{
		if (!m_CommandQueue.IsEmpty())
		{
			error = "Busy: a Lab batch requires no pending commands.";
			return false;
		}
		if (!PrepareParameterValues(values, error))
		{
			return false;
		}
		m_CommandQueue.RequestParameterBatch(id);
		return true;
	}

	void LabRuntime::CancelParameterBatch(uint64_t id) noexcept
	{
		m_CommandQueue.CancelParameterBatch(id);
	}

	std::optional<LabRuntime::PreparedParameterBatch> LabRuntime::PrepareParameterBatch(uint64_t id,
		std::span<const LabParameterValue> values, std::string& error) const noexcept
	{
		if (!m_CommandQueue.HasParameterBatch(id))
		{
			error = "The Lab parameter command lost its owning reservation.";
			return std::nullopt;
		}
		auto prepared = PrepareParameterValues(values, error);
		if (prepared)
		{
			prepared->m_Id = id;
		}
		return prepared;
	}

	void LabRuntime::CommitParameterBatch(PreparedParameterBatch& prepared) noexcept
	{
		GGLAB_ASSERT_MSG(prepared.m_Owner == this && prepared.m_Session == m_ActiveSession.get() &&
			prepared.m_SessionSerial == m_TemporalSessionSerial && prepared.m_Revision == m_ParameterRevision &&
			!prepared.m_Committed && m_IsEntered && IsReady() && !m_PendingSession &&
			m_CommandQueue.HasParameterBatch(prepared.m_Id),
			"A prepared Lab batch must commit once, without intervening callbacks or session changes.");
		const bool consumed = m_CommandQueue.ConsumeParameterBatch(prepared.m_Id);
		GGLAB_ASSERT(consumed);
		GGLAB_UNUSED(consumed);
		m_ActiveSession->CommitPreparedParameters(std::move(prepared.m_Parameters));
		if (prepared.m_HasChanges)
		{
			++m_ParameterRevision;
		}
		prepared.m_Committed = true;
	}

	std::optional<LabRuntime::LightingParameterEffect> LabRuntime::ApplyParameterBatchEffects(
		PreparedParameterBatch&& prepared) noexcept
	{
		GGLAB_ASSERT(prepared.m_Owner == this && prepared.m_Session == m_ActiveSession.get() &&
			prepared.m_SessionSerial == m_TemporalSessionSerial &&
			m_ParameterRevision == prepared.m_Revision + (prepared.m_HasChanges ? 1 : 0) &&
			prepared.m_Committed && prepared.m_Id != 0);
		prepared.m_Id = 0;
		if (prepared.m_HasChanges)
		{
			m_ActiveSession->ApplyImmediateParameters();
			m_ActiveSession->GetCamera().Update();
		}
		if (prepared.m_LightingParameter)
		{
			return LightingParameterEffect{ *prepared.m_LightingParameter,
				m_ActiveSession->ApplyDeferredLightingParameter(*prepared.m_LightingParameter) };
		}
		return std::nullopt;
	}

	bool LabRuntime::ApplyParameterBatch(uint64_t id,
		std::span<const LabParameterValue> values, std::string& error) noexcept
	{
		auto prepared = PrepareParameterBatch(id, values, error);
		if (!prepared)
		{
			return false;
		}
		CommitParameterBatch(*prepared);
		GGLAB_UNUSED(ApplyParameterBatchEffects(std::move(*prepared)));
		return true;
	}
}
