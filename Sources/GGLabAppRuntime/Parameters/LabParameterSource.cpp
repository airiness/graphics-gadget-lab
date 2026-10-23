#include "Parameters/ParameterSourceAdapters.h"
#include "Lab/LabRuntime.h"

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
				if (!LabValuesEqual(parameters->Find(value.m_Id)->m_Value, value.m_Value))
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
		const auto* session = m_Lab ? m_Lab->GetActiveSession() : nullptr;
		return session ? &session->GetParameters() : nullptr;
	}

	bool LabParameterSource::Supports(const LabParameterDesc& desc) const noexcept
	{
		return m_Lab && m_Lab->SupportsParameterEffect(desc);
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
		auto prepared = m_Lab->PrepareParameterBatch(id, values, error);
		return prepared ? std::make_unique<Prepared>(*this, std::move(*prepared), values) : nullptr;
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
				value.m_Id, { frame.m_ViewSettings, frame.m_View });
			return effective && LabValuesEqual(*effective, value.m_Value);
		}
		return LabValuesEqual(current->m_Value, value.m_Value);
	}
}
