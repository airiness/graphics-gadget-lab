#include "Parameters/ParameterSourceAdapters.h"
#include "Lab/LabRuntime.h"
#include "Lab/LabSessionBase.h"

#include <utility>

namespace gglab
{
	class LabParameterSource::Prepared final : public PreparedParameterSourceBase
	{
	public:
		Prepared(LabParameterSource& owner, LabRuntime::PreparedParameterBatch prepared,
			std::span<const LabParameterValue> values) noexcept : m_Owner(owner), m_Prepared(std::move(prepared))
		{
			const auto* parameters = owner.GetParameters();
			for (const auto& value : values)
			{
				if (!LabValuesEqual(parameters->Find(value.m_Id)->m_Value, value.m_Value) ||
					(m_Prepared.HasWorldLightingChanges() &&
						GetParameterEffectClass(parameters->Find(value.m_Id)->m_Desc) == ParameterEffectClass::Deferred))
				{
					m_Changed.push_back(value.m_Id);
				}
			}
		}

		void Commit() noexcept override { m_Owner.m_Lab->CommitParameterBatch(m_Prepared); }
		void ApplyEffects() noexcept override
		{
			m_Effect = m_Owner.m_Lab->ApplyParameterBatchEffects(std::move(m_Prepared));
		}
		ParameterApplication DescribeApplication(const LabParameterId& id,
			ParameterDomainState& domain) const noexcept override
		{
			const auto* parameter = m_Owner.GetParameters()->Find(id);
			if (GetParameterEffectClass(parameter->m_Desc) == ParameterEffectClass::Rebuild)
			{
				const auto generation = m_Prepared.GetRebuildGeneration();
				if (!IsChanged(id) || generation == 0)
				{
					return { .m_NoChange = true };
				}
				const ParameterStateIdentity identity{ .m_Domain = "lab-session", .m_Generation = generation };
				const auto lifetime = m_Owner.ObserveLifetime(identity);
				return { .m_ActualClass = ParameterEffectClass::Rebuild, .m_Identity = identity,
					.m_Failure = lifetime.m_Failure };
			}
			if (GetParameterEffectClass(parameter->m_Desc) != ParameterEffectClass::Deferred)
			{
				return { .m_NoChange = !IsChanged(id) };
			}
			const auto evidence = m_Effect && m_Effect->m_Parameter == id ? m_Effect->m_Result : LightingMutationResult{};
			return m_Owner.m_Domain.Translate(evidence, !IsChanged(id), domain);
		}

	private:
		LabParameterSource& m_Owner;
		LabRuntime::PreparedParameterBatch m_Prepared;
		std::optional<LabRuntime::LightingParameterEffect> m_Effect;
	};

	const LabParameterSet* LabParameterSource::GetParameters() const noexcept
	{
		return m_Lab ? m_Lab->GetParameterSourceParameters() : nullptr;
	}

	bool LabParameterSource::Supports(const LabParameterDesc& desc) const noexcept
	{
		const auto* session = m_Lab ? m_Lab->GetActiveSession() : nullptr;
		return session && m_Lab->SupportsParameterEffect(desc) &&
			(!session->SupportsWorldLightingParameter(desc.m_Id) || m_Control);
	}

	bool LabParameterSource::Reserve(uint64_t id, std::span<const LabParameterValue> values,
		std::string& error) noexcept
	{
		return m_Lab && m_Lab->QueueParameterBatch(id, values, error);
	}

	void LabParameterSource::Cancel(uint64_t id) noexcept
	{
		if (m_Lab)
		{
			m_Lab->CancelParameterBatch(id);
		}
	}

	std::unique_ptr<PreparedParameterSourceBase> LabParameterSource::Prepare(uint64_t id,
		std::span<const LabParameterValue> values, std::string& error) noexcept
	{
		auto prepared = m_Lab->PrepareParameterBatch(id, values, error, m_Control, m_Identity);
		return prepared ? std::make_unique<Prepared>(*this, std::move(*prepared), values) : nullptr;
	}

	bool LabParameterSource::ContinuesContentChange(uint64_t batch,
		uint64_t previous, uint64_t current) const noexcept
	{
		const auto* rebuild = m_Lab ? m_Lab->GetParameterRebuild() : nullptr;
		return rebuild && rebuild->m_BatchId == batch &&
			rebuild->m_State == LabRuntime::ParameterRebuildState::Published &&
			(previous >> 32) == (current >> 32) &&
			static_cast<uint32_t>(previous) == rebuild->m_SourceSession &&
			static_cast<uint32_t>(current) == rebuild->m_TargetSession;
	}

	ParameterPublicationObservation LabParameterSource::ObserveLifetime(
		const ParameterStateIdentity& identity) const noexcept
	{
		if (identity.m_Domain != "lab-session")
		{
			return {};
		}
		const auto* rebuild = m_Lab ? m_Lab->GetParameterRebuild() : nullptr;
		if (!rebuild || rebuild->m_Generation != identity.m_Generation ||
			rebuild->m_State == LabRuntime::ParameterRebuildState::Superseded)
		{
			return { .m_State = ParameterPublication::Superseded };
		}
		if (rebuild->m_State == LabRuntime::ParameterRebuildState::Failed)
		{
			return { .m_State = ParameterPublication::Failed, .m_Failure = rebuild->m_Failure };
		}
		return {};
	}

	ParameterPublicationObservation LabParameterSource::ObservePublication(
		const ParameterStateIdentity& identity, const ParameterSourceFrame& frame) const noexcept
	{
		if (identity.m_Domain != "lab-session")
		{
			return m_Domain.ObservePublication(identity, frame);
		}
		auto observation = ObserveLifetime(identity);
		const auto* rebuild = m_Lab ? m_Lab->GetParameterRebuild() : nullptr;
		if (observation.m_State == ParameterPublication::Pending && rebuild &&
			rebuild->m_State == LabRuntime::ParameterRebuildState::Published &&
			frame.m_ContentIdentity == m_Identity &&
			static_cast<uint32_t>(frame.m_ContentIdentity) == rebuild->m_TargetSession)
		{
			observation.m_State = ParameterPublication::Published;
		}
		return observation;
	}

	bool LabParameterSource::MatchesFrame(const LabParameterValue& value,
		const ParameterSourceFrame& frame) const noexcept
	{
		const auto* parameters = GetParameters();
		const auto* current = parameters ? parameters->Find(value.m_Id) : nullptr;
		if (!current)
		{
			return false;
		}
		if (GetParameterEffectClass(current->m_Desc) == ParameterEffectClass::Immediate)
		{
			const auto effective = m_Lab->GetActiveSession()->ReadImmediateParameterFrameValue(
				value.m_Id, { frame.m_ViewSettings, frame.m_View, frame.m_Scene, frame.m_FrameSerial });
			return effective && LabValuesEqual(*effective, value.m_Value);
		}
		return LabValuesEqual(current->m_Value, value.m_Value);
	}
}
