#include "Parameters/ParameterSourceAdapters.h"
#include "Demo/DemoBase.h"
#include "GGLabRuntime/Graphics/WorldLightingControlBase.h"

#include <algorithm>
#include <utility>

namespace gglab
{
	class LightingProfileParameterSource::Prepared final : public PreparedParameterSourceBase
	{
	public:
		Prepared(LightingProfileParameterSource& owner, const LabParameterId& id,
			LabParameterSet parameters, std::unique_ptr<PreparedWorldLightingMutationBase> mutation) noexcept :
			m_Owner(owner), m_Id(id), m_Parameters(std::move(parameters)), m_Mutation(std::move(mutation))
		{
			if (m_Mutation->HasChanges())
			{
				m_Changed.push_back(id);
			}
		}
		void Commit() noexcept override
		{
			m_Evidence = m_Mutation->Commit();
			m_Owner.m_Parameters = std::move(m_Parameters);
		}
		ParameterApplication DescribeApplication(const LabParameterId& id,
			ParameterDomainState& domain) const noexcept override
		{
			GGLAB_ASSERT(id == m_Id);
			return m_Owner.m_Domain.Translate(m_Evidence, !IsChanged(id), domain);
		}

	private:
		LightingProfileParameterSource& m_Owner;
		LabParameterId m_Id;
		LabParameterSet m_Parameters;
		std::unique_ptr<PreparedWorldLightingMutationBase> m_Mutation;
		LightingMutationResult m_Evidence;
	};

	void LightingProfileParameterSource::Bind(DemoBase* demo, uint64_t identity) noexcept
	{
		m_Demo = demo;
		m_Identity = identity;
		m_Parameters = {};
		m_Profiles.clear();
		m_PreparedProfiles.clear();
		m_Hashes.clear();
		m_Descriptor.reset();
		m_Publication = {};
		m_Error.clear();
		m_Initialized = false;
	}

	void LightingProfileParameterSource::Synchronize() noexcept
	{
		if (m_Control && m_Demo)
		{
			m_Control->SynchronizeWorldLighting(m_Demo->GetWorld(), m_Identity);
		}
	}

	bool LightingProfileParameterSource::ValidateMissing(const LabParameterId& id, std::string& error) const noexcept
	{
		if (id.GetName() == "environment.profile" && !m_Error.empty())
		{
			error = m_Error;
			return false;
		}
		return true;
	}

	bool LightingProfileParameterSource::Validate(const LabParameterValue& value, std::string& error) const noexcept
	{
		const auto* index = std::get_if<int32_t>(&value.m_Value);
		if (!index || *index < 0 || static_cast<size_t>(*index) >= m_Profiles.size() ||
			!m_Control || !m_Demo || !CanApplyWorldLightingInputs(m_Demo->GetWorld()))
		{
			error = "Select a registered lighting profile; custom is read-only and requires a designated World sun.";
			return false;
		}
		return true;
	}

	std::unique_ptr<PreparedParameterSourceBase> LightingProfileParameterSource::Prepare(uint64_t id,
		std::span<const LabParameterValue> values, std::string& error) noexcept
	{
		GGLAB_UNUSED(id);
		GGLAB_ASSERT(values.size() == 1);
		const auto* selected = std::get_if<int32_t>(&values.front().m_Value);
		if (!selected || *selected < 0 || static_cast<size_t>(*selected) >= m_Profiles.size() || !m_Control || !m_Demo)
		{
			error = "ParameterPreparationFailed: select a registered lighting profile.";
			return {};
		}
		auto parameters = m_Parameters;
		if (!parameters.ApplyBatch(values, error))
		{
			return {};
		}
		const auto index = std::get<int32_t>(values.front().m_Value);
		auto mutation = m_Control->PrepareWorldLightingInputs(m_Demo->GetWorld(), m_Identity,
			m_Profiles[index].m_Inputs, error);
		if (!mutation)
		{
			return {};
		}
		return std::make_unique<Prepared>(*this, values.front().m_Id, std::move(parameters), std::move(mutation));
	}

	bool LightingProfileParameterSource::MatchesFrame(const LabParameterValue& value,
		const ParameterSourceFrame& frame) const noexcept
	{
		GGLAB_UNUSED(frame);
		const auto* current = m_Parameters.Find(value.m_Id);
		return current && FindCurrent() == std::get<int32_t>(value.m_Value) &&
			LabValuesEqual(current->m_Value, value.m_Value);
	}

	int32_t LightingProfileParameterSource::FindCurrent() const noexcept
	{
		if (!m_Control)
		{
			return -1;
		}
		const auto publication = m_Control->GetWorldLightingPublication();
		if (!publication.m_Requested || publication.m_Requested->m_SessionIdentity != m_Identity)
		{
			return -1;
		}
		for (size_t index = 0; index < m_PreparedProfiles.size(); ++index)
		{
			if (EqualWorldLightingEffect(*publication.m_Requested, m_PreparedProfiles[index]))
			{
				return static_cast<int32_t>(index);
			}
		}
		return -1;
	}

	void LightingProfileParameterSource::Refresh(bool available) noexcept
	{
		m_Parameters = {};
		if (!m_Demo)
		{
			return;
		}
		if (!m_Initialized && available)
		{
			m_Initialized = true;
			const auto profiles = m_Demo->GetLightingProfiles();
			if (profiles.size() > 64 || (!profiles.empty() && !m_Control))
			{
				m_Error = "LightingProfileRegistration: at most 64 profiles and "
					"an explicit World Lighting control are required.";
			}
			else
			{
				for (const auto& profile : profiles)
				{
					if (!ValidateLightingProfile(profile))
					{
						m_Error = "LightingProfileRegistration: invalid metadata or lighting inputs in '" +
							profile.m_Id + "'.";
						break;
					}
					const auto hash = HashLightingProfile(profile);
					const auto prepared = PrepareWorldLightingInputs(profile.m_Inputs);
					if (!hash.IsValid() || !prepared.m_AuthoringHash.IsValid() ||
						std::ranges::find(m_Hashes, hash) != m_Hashes.end() ||
						std::ranges::any_of(m_Profiles, [&](const auto& existing)
							{ return existing.m_Id == profile.m_Id; }) ||
						std::ranges::any_of(m_PreparedProfiles, [&](const auto& existing)
							{ return EqualWorldLightingEffect(existing, prepared); }))
					{
						m_Error = "LightingProfileRegistration: duplicate id/hash, "
							"equivalent effective inputs or unavailable hash in '" + profile.m_Id + "'.";
						break;
					}
					m_Profiles.push_back(profile);
					m_PreparedProfiles.push_back(prepared);
					m_Hashes.push_back(hash);
				}
			}
			if (!m_Error.empty())
			{
				m_Profiles.clear();
				m_PreparedProfiles.clear();
				m_Hashes.clear();
			}
		}
		if (m_Profiles.empty())
		{
			return;
		}
		if (!m_Descriptor)
		{
			m_Descriptor = LabParameterDesc{ .m_Id = LabParameterId("environment.profile"),
				.m_Name = "Lighting Profile", .m_Group = "Environment", .m_Type = LabParameterType::Enum,
				.m_DefaultValue = int32_t(-1), .m_EnumItems = { { -1, "custom (read-only)" } },
				.m_EffectClass = ParameterEffectClass::Deferred };
			for (size_t index = 0; index < m_Profiles.size(); ++index)
			{
				m_Descriptor->m_EnumItems.push_back({ static_cast<int32_t>(index), m_Profiles[index].m_Id });
			}
		}
		auto desc = *m_Descriptor;
		desc.m_DefaultValue = FindCurrent();
		GGLAB_UNUSED(m_Parameters.Add(std::move(desc)));
	}

	void LightingProfileParameterSource::ObservePublication(uint64_t lightingGeneration) noexcept
	{
		m_Publication = {};
		if (!m_Control)
		{
			return;
		}
		const auto publication = m_Control->GetWorldLightingPublication();
		const auto identify = [&](const std::optional<WorldLightingState>& state) -> std::optional<LightingProfileState>
			{
				if (!state || state->m_Generation == 0 || state->m_SessionIdentity != m_Identity)
				{
					return std::nullopt;
				}
				for (size_t index = 0; index < m_PreparedProfiles.size(); ++index)
				{
					if (EqualWorldLightingAuthoring(*state, m_PreparedProfiles[index]))
					{
						return LightingProfileState{ m_Profiles[index].m_Id,
							m_Hashes[index], state->m_Generation };
					}
				}
				return std::nullopt;
			};
		m_Publication.m_Requested = identify(publication.m_Requested);
		if (publication.m_Active && publication.m_Active->m_Generation == lightingGeneration)
		{
			m_Publication.m_Active = identify(publication.m_Active);
		}
	}
}
