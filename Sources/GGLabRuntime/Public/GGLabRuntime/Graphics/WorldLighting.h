#pragma once
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingSettings.h"
#include "GGLabRuntime/Graphics/WorldSun.h"

#include <cstdint>
#include <optional>

namespace gglab
{
	class World;

	struct WorldLightingInputs
	{
		Vector3 m_SunDirection = -Vector3::UnitY;
		WorldSunSettings m_Sun{};
		AtmosphereSettings m_Atmosphere{};
		EnvironmentBackgroundMode m_BackgroundMode = EnvironmentBackgroundMode::PhysicalSky;
		float m_IBLIntensity = 1.0f;
		float m_IBLRotationRadians = 0.0f;
	};

	// Immutable provenance of an actual Runtime request/publication. A source that
	// deduplicates to an existing request never replaces that request's authored inputs.
	struct WorldLightingState
	{
		WorldLightingInputs m_Inputs{};
		ResolvedWorldSun m_Sun{};
		AtmosphereGPU m_Parameters{};
		Vector3 m_ReferenceObserverWorld{};
		// Runtime comparison only, distinct from a persisted profile values hash.
		Sha256Digest m_AuthoringHash;
		uint64_t m_Generation = 0;
		uint64_t m_SessionIdentity = 0;
	};

	struct WorldLightingPublication
	{
		std::optional<WorldLightingState> m_Requested;
		std::optional<WorldLightingState> m_Active;
	};

	[[nodiscard]] bool ValidateWorldLightingInputs(const WorldLightingInputs& inputs) noexcept;
	[[nodiscard]] bool CanApplyWorldLightingInputs(World& world) noexcept;
	[[nodiscard]] Quaternion ResolveWorldLightingRotation(const Vector3& direction) noexcept;
	[[nodiscard]] bool ApplyWorldLightingInputsToWorld(World& world, const WorldLightingInputs& inputs) noexcept;
	// Preparation uses the same direction extraction and reference observer as Runtime.
	[[nodiscard]] WorldLightingState PrepareWorldLightingInputs(const WorldLightingInputs& inputs) noexcept;
	[[nodiscard]] WorldLightingState ResolveWorldLightingInputs(const WorldLightingInputs& inputs,
		const ResolvedWorldSun& sun) noexcept;
	// Runtime-only fingerprint; direction must already be extracted/resolved.
	[[nodiscard]] Sha256Digest HashWorldLightingAuthoring(const WorldLightingInputs& inputs) noexcept;
	[[nodiscard]] bool EqualWorldLightingEffect(const WorldLightingState& lhs,
		const WorldLightingState& rhs) noexcept;
	[[nodiscard]] bool EqualWorldLightingAuthoring(const WorldLightingState& lhs,
		const WorldLightingState& rhs) noexcept;
}
