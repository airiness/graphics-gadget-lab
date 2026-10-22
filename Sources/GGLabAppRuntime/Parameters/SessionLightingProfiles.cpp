#include "Parameters/SessionParameterControl.h"
#include "Demo/DemoBase.h"
#include "GGLabRuntime/Graphics/WorldLightingControlBase.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace gglab
{
	bool SessionParameterControl::IsLightingProfileParameter(const LabParameterId& id) const noexcept
	{
		return !m_LightingProfiles.empty() && m_HostParameters.Find(id) &&
			id.GetName() == "environment.profile";
	}

	int32_t SessionParameterControl::FindCurrentLightingProfile() const noexcept
	{
		if (!m_WorldLighting)
		{
			return -1;
		}
		const auto publication = m_WorldLighting->GetWorldLightingPublication();
		if (!publication.m_Requested || publication.m_Requested->m_SessionIdentity != m_ContentSerial)
		{
			return -1;
		}
		for (size_t index = 0; index < m_PreparedLightingProfiles.size(); ++index)
		{
			if (EqualWorldLightingEffect(*publication.m_Requested, m_PreparedLightingProfiles[index]))
			{
				return static_cast<int32_t>(index);
			}
		}
		return -1;
	}

	void SessionParameterControl::RefreshLightingProfiles(LabParameterSet& host) noexcept
	{
		if (!m_ProfilesInitialized && IsAvailable())
		{
			m_ProfilesInitialized = true;
			const auto profiles = m_Demo->GetLightingProfiles();
			if (profiles.size() > 64 || (!profiles.empty() && !m_WorldLighting))
			{
				m_ProfileRegistrationError = "LightingProfileRegistration: at most 64 profiles and "
					"an explicit World Lighting control are required.";
			}
			else
			{
				for (const auto& profile : profiles)
				{
					if (!ValidateLightingProfile(profile))
					{
						m_ProfileRegistrationError = "LightingProfileRegistration: invalid metadata or lighting inputs in '" +
							profile.m_Id + "'.";
						break;
					}
					const auto hash = HashLightingProfile(profile);
					const auto prepared = PrepareWorldLightingInputs(profile.m_Inputs);
					if (!hash.IsValid() || !prepared.m_AuthoringHash.IsValid() ||
						std::ranges::find(m_LightingProfileHashes, hash) != m_LightingProfileHashes.end() ||
						std::ranges::any_of(m_LightingProfiles, [&](const auto& existing)
							{ return existing.m_Id == profile.m_Id; }) ||
						std::ranges::any_of(m_PreparedLightingProfiles, [&](const auto& existing)
							{ return EqualWorldLightingEffect(existing, prepared); }))
					{
						m_ProfileRegistrationError = "LightingProfileRegistration: duplicate id/hash, "
							"equivalent effective inputs or unavailable hash in '" + profile.m_Id + "'.";
						break;
					}
					m_LightingProfiles.push_back(profile);
					m_PreparedLightingProfiles.push_back(prepared);
					m_LightingProfileHashes.push_back(hash);
				}
			}
			if (!m_ProfileRegistrationError.empty())
			{
				m_LightingProfiles.clear();
				m_PreparedLightingProfiles.clear();
				m_LightingProfileHashes.clear();
			}
		}
		if (m_LightingProfiles.empty())
		{
			return;
		}
		if (!m_ProfileDescriptor)
		{
			m_ProfileDescriptor = LabParameterDesc{ .m_Id = LabParameterId("environment.profile"),
				.m_Name = "Lighting Profile", .m_Group = "Environment", .m_Type = LabParameterType::Enum,
				.m_DefaultValue = int32_t(-1), .m_EnumItems = { { -1, "custom (read-only)" } },
				.m_EffectClass = ParameterEffectClass::Deferred };
			for (size_t index = 0; index < m_LightingProfiles.size(); ++index)
			{
				m_ProfileDescriptor->m_EnumItems.push_back({ static_cast<int32_t>(index), m_LightingProfiles[index].m_Id });
			}
		}
		auto desc = *m_ProfileDescriptor;
		desc.m_DefaultValue = FindCurrentLightingProfile();
		GGLAB_UNUSED(host.Add(std::move(desc)));
	}

	void SessionParameterControl::UpdateLightingProfilePublication(uint64_t lightingGeneration) noexcept
	{
		m_LightingProfilesState = {};
		if (!m_WorldLighting)
		{
			return;
		}
		const auto publication = m_WorldLighting->GetWorldLightingPublication();
		const auto identify = [&](const std::optional<WorldLightingState>& state) -> std::optional<LightingProfileState>
			{
				if (!state || state->m_Generation == 0 || state->m_SessionIdentity != m_ContentSerial)
				{
					return std::nullopt;
				}
				for (size_t index = 0; index < m_PreparedLightingProfiles.size(); ++index)
				{
					if (EqualWorldLightingAuthoring(*state, m_PreparedLightingProfiles[index]))
					{
						return LightingProfileState{ m_LightingProfiles[index].m_Id,
							m_LightingProfileHashes[index], state->m_Generation };
					}
				}
				return std::nullopt;
			};
		m_LightingProfilesState.m_Requested = identify(publication.m_Requested);
		if (publication.m_Active && publication.m_Active->m_Generation == lightingGeneration)
		{
			m_LightingProfilesState.m_Active = identify(publication.m_Active);
		}
	}
}
