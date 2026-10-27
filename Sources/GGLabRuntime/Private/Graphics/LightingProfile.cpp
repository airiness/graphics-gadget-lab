#include "GGLabRuntime/Graphics/LightingProfile.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Scene/Components.h"
#include "Graphics/RenderWorldExtractor.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <bit>
#include <cstring>
#include <string_view>

namespace gglab
{
	namespace
	{
		void AddFloat(Sha256Builder& hash, float value) noexcept
		{
			GGLAB_UNUSED(hash.AddU32LE(std::bit_cast<uint32_t>(value == 0.0f ? 0.0f : value)));
		}

		void AddVector(Sha256Builder& hash, const Vector3& value) noexcept
		{
			AddFloat(hash, value.m_X);
			AddFloat(hash, value.m_Y);
			AddFloat(hash, value.m_Z);
		}

		Sha256Digest HashInputs(const WorldLightingInputs& inputs, std::string_view domain) noexcept
		{
			Sha256Builder hash;
			GGLAB_UNUSED(hash.AddStringUtf8(domain));
			AddVector(hash, inputs.m_SunDirection);
			AddFloat(hash, inputs.m_Sun.m_PerpendicularIlluminanceLux);
			AddVector(hash, inputs.m_Sun.m_Chromaticity);
			AddFloat(hash, inputs.m_Sun.m_AngularRadiusDegrees);
			const auto& a = inputs.m_Atmosphere;
			AddFloat(hash, a.m_PlanetRadiusMeters);
			AddFloat(hash, a.m_HeightMeters);
			AddVector(hash, a.m_PlanetCenterMeters);
			AddFloat(hash, a.m_WorldUnitsToMeters);
			AddVector(hash, a.m_RayleighScattering);
			AddFloat(hash, a.m_RayleighScaleHeightMeters);
			AddFloat(hash, a.m_MieScattering);
			AddFloat(hash, a.m_MieExtinction);
			AddFloat(hash, a.m_MieScaleHeightMeters);
			AddFloat(hash, a.m_MieAnisotropy);
			AddVector(hash, a.m_Absorption);
			AddFloat(hash, a.m_AbsorptionCenterMeters);
			AddFloat(hash, a.m_AbsorptionHalfWidthMeters);
			AddVector(hash, a.m_GroundAlbedo);
			GGLAB_UNUSED(hash.AddU8(static_cast<uint8_t>(inputs.m_BackgroundMode)));
			AddFloat(hash, inputs.m_IBLIntensity);
			AddFloat(hash, inputs.m_IBLRotationRadians);
			return hash.Finish();
		}
	}

	Sha256Digest HashLightingProfile(const WorldLightingProfile& profile) noexcept
	{
		if (!ValidateWorldLightingInputs(profile.m_Inputs))
		{
			return {};
		}
		return HashInputs(PrepareWorldLightingInputs(profile.m_Inputs).m_Inputs,
			"gglab.lighting-profile.inputs.v2");
	}

	bool ValidateWorldLightingInputs(const WorldLightingInputs& inputs) noexcept
	{
		return math::IsFinite(inputs.m_SunDirection) &&
			math::IsFinite(inputs.m_SunDirection.LengthSquared()) &&
			inputs.m_SunDirection.LengthSquared() > 1.0e-8f &&
			ValidateWorldSunSettings(inputs.m_Sun) && ValidateAtmosphereSettings(inputs.m_Atmosphere) &&
			inputs.m_BackgroundMode == EnvironmentBackgroundMode::PhysicalSky &&
			inputs.m_IBLIntensity == 1.0f && inputs.m_IBLRotationRadians == 0.0f;
	}

	bool ValidateLightingProfile(const WorldLightingProfile& profile) noexcept
	{
		return !profile.m_Id.empty() && profile.m_Id.size() <= 128 &&
			ValidateWorldLightingInputs(profile.m_Inputs) &&
			profile.m_Inputs.m_Sun.m_PerpendicularIlluminanceLux > 0.0f &&
			(!profile.m_RecommendedEV100 || Camera::IsValidManualEV100(*profile.m_RecommendedEV100));
	}

	Sha256Digest HashWorldLightingAuthoring(const WorldLightingInputs& inputs) noexcept
	{
		return HashInputs(inputs, "gglab.world-lighting.authoring.v1");
	}

	WorldLightingState ResolveWorldLightingInputs(const WorldLightingInputs& inputs,
		const ResolvedWorldSun& sun) noexcept
	{
		WorldLightingState state{ .m_Inputs = inputs, .m_Sun = sun };
		state.m_Inputs.m_SunDirection = sun.m_Direction;
		const auto resolved = ResolveAtmosphere(inputs.m_Atmosphere, sun, Vector3::Zero);
		const Vector3 observerMeters = Vector3(resolved.m_World.m_X, resolved.m_World.m_Y,
			resolved.m_World.m_Z) * 1000.0f + Vector3::UnitY * (resolved.m_Radii.m_X * 1000.0f + 1.0f);
		state.m_ReferenceObserverWorld = observerMeters / (resolved.m_World.m_W * 1000.0f);
		state.m_Parameters = ResolveAtmosphere(inputs.m_Atmosphere, sun, state.m_ReferenceObserverWorld);
		return state;
	}

	Quaternion ResolveWorldLightingRotation(const Vector3& direction) noexcept
	{
		Vector3 normalized = direction;
		normalized.Normalize();
		return math::RotationFromTo(Vector3::Forward, normalized);
	}

	WorldLightingState PrepareWorldLightingInputs(const WorldLightingInputs& inputs) noexcept
	{
		const auto rotation = ResolveWorldLightingRotation(inputs.m_SunDirection);
		auto state = ResolveWorldLightingInputs(inputs,
			ResolveWorldSun(inputs.m_Sun, ResolveWorldSunDirection(rotation)));
		state.m_AuthoringHash = HashWorldLightingAuthoring(state.m_Inputs);
		return state;
	}

	bool EqualWorldLightingEffect(const WorldLightingState& lhs, const WorldLightingState& rhs) noexcept
	{
		return lhs.m_Sun.m_Direction.m_X == rhs.m_Sun.m_Direction.m_X &&
			lhs.m_Sun.m_Direction.m_Y == rhs.m_Sun.m_Direction.m_Y &&
			lhs.m_Sun.m_Direction.m_Z == rhs.m_Sun.m_Direction.m_Z &&
			std::memcmp(&lhs.m_Parameters, &rhs.m_Parameters, sizeof(AtmosphereGPU)) == 0;
	}

	bool EqualWorldLightingAuthoring(const WorldLightingState& lhs, const WorldLightingState& rhs) noexcept
	{
		return lhs.m_AuthoringHash.IsValid() && rhs.m_AuthoringHash.IsValid() &&
			lhs.m_AuthoringHash == rhs.m_AuthoringHash;
	}

	bool CanApplyWorldLightingInputs(World& world) noexcept
	{
		return RenderWorldExtractor{}.Extract(world).m_MainDirectionalLight.m_WorldSun.has_value();
	}

	bool ApplyWorldLightingInputsToWorld(World& world, const WorldLightingInputs& inputs) noexcept
	{
		if (!ValidateWorldLightingInputs(inputs))
		{
			return false;
		}
		auto light = RenderWorldExtractor{}.Extract(world).m_MainDirectionalLight;
		if (!light.m_WorldSun)
		{
			return false;
		}
		light.m_Transform->m_Rotation = ResolveWorldLightingRotation(inputs.m_SunDirection);
		light.m_Light->m_WorldSun = inputs.m_Sun;
		world.m_Atmosphere = inputs.m_Atmosphere;
		world.MarkLightingAuthoringChanged();
		return true;
	}
}
