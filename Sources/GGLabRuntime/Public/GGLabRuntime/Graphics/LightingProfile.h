#pragma once
#include "GGLabRuntime/Graphics/WorldLighting.h"

#include <cstdint>
#include <optional>
#include <string>

namespace gglab
{
	inline constexpr uint32_t LightingProfileHashVersion = 2;

	// Content metadata is not interpreted by the World Lighting control.
	struct WorldLightingProfile
	{
		std::string m_Id;
		WorldLightingInputs m_Inputs{};
		std::optional<float> m_RecommendedEV100;
	};

	// Hash v2 encodes explicit little-endian float32 fields, canonicalizes direction
	// through the producer's rotation/extraction path and normalizes signed zero.
	// Other authored fields stay raw; id and non-applied exposure advice are excluded.
	// Invalid/unsupported inputs produce an invalid digest.
	[[nodiscard]] Sha256Digest HashLightingProfile(const WorldLightingProfile& profile) noexcept;
	[[nodiscard]] bool ValidateLightingProfile(const WorldLightingProfile& profile) noexcept;

	struct LightingProfileState
	{
		std::string m_Id;
		Sha256Digest m_Hash;
		uint64_t m_Generation = 0;
	};

	struct LightingProfilePublication
	{
		std::optional<LightingProfileState> m_Requested;
		std::optional<LightingProfileState> m_Active;
	};
}
