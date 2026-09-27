#include "GGLabRuntime/Graphics/Atmosphere.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace gglab
{
	namespace
	{
		float Bound(float value, float low, float high, float fallback) noexcept
		{
			return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
		}
		Vector3 Coefficients(const Vector3& value) noexcept
		{
			return Vector3(Bound(value.m_X, 0.0f, 0.001f, 0.0f),
				Bound(value.m_Y, 0.0f, 0.001f, 0.0f), Bound(value.m_Z, 0.0f, 0.001f, 0.0f)) * 1000.0f;
		}
	}
	AtmosphereGPU ResolveAtmosphere(const AtmosphereSettings& s, const ResolvedWorldSun& sun,
		const Vector3& camera) noexcept
	{
		AtmosphereGPU a{};
		const float bottom = Bound(s.m_PlanetRadiusMeters, 100000.0f, 100000000.0f, 6360000.0f) * 0.001f;
		const float height = Bound(s.m_HeightMeters, 1000.0f, 1000000.0f, 100000.0f) * 0.001f;
		a.m_Radii = Vector4(bottom, bottom + height, 0.0f, 0.0f);
		const auto ray = Coefficients(s.m_RayleighScattering);
		a.m_Rayleigh = Vector4(ray.m_X, ray.m_Y, ray.m_Z,
			Bound(s.m_RayleighScaleHeightMeters, 100.0f, 100000.0f, 8000.0f) * 0.001f);
		const float mie = Bound(s.m_MieScattering, 0.0f, 0.001f, 0.0f) * 1000.0f;
		a.m_Mie = Vector4(mie, std::max(mie, Bound(s.m_MieExtinction, 0.0f, 0.001f, 0.0f) * 1000.0f),
			Bound(s.m_MieScaleHeightMeters, 100.0f, 100000.0f, 1200.0f) * 0.001f,
			Bound(s.m_MieAnisotropy, -0.95f, 0.95f, 0.8f));
		const auto absorption = Coefficients(s.m_Absorption);
		a.m_Absorption = Vector4(absorption.m_X, absorption.m_Y, absorption.m_Z,
			Bound(s.m_AbsorptionCenterMeters, 0.0f, height * 1000.0f, 25000.0f) * 0.001f);
		a.m_Ground = Vector4(Bound(s.m_GroundAlbedo.m_X, 0.0f, 0.95f, 0.3f),
			Bound(s.m_GroundAlbedo.m_Y, 0.0f, 0.95f, 0.3f), Bound(s.m_GroundAlbedo.m_Z, 0.0f, 0.95f, 0.3f),
			Bound(s.m_AbsorptionHalfWidthMeters, 100.0f, 1000000.0f, 15000.0f) * 0.001f);
		a.m_Sun = Vector4(sun.m_DirectIlluminance.m_X, sun.m_DirectIlluminance.m_Y,
			sun.m_DirectIlluminance.m_Z, sun.m_AngularRadiusRadians);
		const double scale = Bound(s.m_WorldUnitsToMeters, 0.0001f, 10000.0f, 1.0f);
		const double x = static_cast<double>(camera.m_X) * scale - s.m_PlanetCenterMeters.m_X;
		const double y = static_cast<double>(camera.m_Y) * scale - s.m_PlanetCenterMeters.m_Y;
		const double z = static_cast<double>(camera.m_Z) * scale - s.m_PlanetCenterMeters.m_Z;
		const double radius = std::sqrt(x*x + y*y + z*z);
		const bool valid = std::isfinite(radius) && radius > 1.0;
		const float mu = valid ? static_cast<float>(-(x * sun.m_Direction.m_X + y * sun.m_Direction.m_Y +
			z * sun.m_Direction.m_Z) / radius) : -sun.m_Direction.m_Y;
		a.m_Observer = Vector4(valid ? std::clamp(static_cast<float>(radius * 0.001), bottom + 0.001f,
			bottom + height - 0.001f) : bottom + 0.001f, std::clamp(mu, -1.0f, 1.0f), 0.0f, 0.0f);
		return a;
	}
	uint32_t AtmosphereDirtyMask(const AtmosphereGPU& previous, const AtmosphereGPU& current) noexcept
	{
		auto p = previous;
		auto c = current;
		p.m_Sun = c.m_Sun = Vector4::Zero;
		p.m_Observer = c.m_Observer = Vector4::Zero;
		p.m_Mie.m_W = c.m_Mie.m_W = 0.0f; // isotropic multiple-scattering closure
		const bool multiple = std::memcmp(&p, &c, sizeof(p)) != 0;
		p.m_Ground.m_X = p.m_Ground.m_Y = p.m_Ground.m_Z = 0.0f;
		c.m_Ground.m_X = c.m_Ground.m_Y = c.m_Ground.m_Z = 0.0f;
		const bool transmittance = std::memcmp(&p, &c, sizeof(p)) != 0;
		return transmittance ? 7u : multiple ? 6u : std::memcmp(&previous, &current, sizeof(current)) != 0 ? 4u : 0u;
	}
	Vector3 EvaluateAtmosphereTransmittance(const AtmosphereGPU& a, float altitudeMeters,
		float mu, float distanceMeters) noexcept
	{
		const double r = a.m_Radii.m_X + std::max(0.0f, altitudeMeters) * 0.001;
		const double step = std::max(0.0f, distanceMeters) * 0.001 / 256.0;
		Vector3 opticalDepth = Vector3::Zero;
		for (int i = 0; i < 256; ++i)
		{
			const double t = (i + 0.5) * step;
			const float h = static_cast<float>(std::sqrt(r*r + t*t + 2.0*r*t*mu) - a.m_Radii.m_X);
			if (h < 0.0f) return Vector3::Zero;
			if (h > a.m_Radii.m_Y - a.m_Radii.m_X) continue;
			const float ray = std::exp(-h / a.m_Rayleigh.m_W);
			const float mie = std::exp(-h / a.m_Mie.m_Z);
			const float ozone = std::max(0.0f, 1.0f - std::abs(h-a.m_Absorption.m_W)/a.m_Ground.m_W);
			opticalDepth += Vector3(a.m_Rayleigh.m_X*ray+a.m_Mie.m_Y*mie+a.m_Absorption.m_X*ozone,
				a.m_Rayleigh.m_Y*ray+a.m_Mie.m_Y*mie+a.m_Absorption.m_Y*ozone,
				a.m_Rayleigh.m_Z*ray+a.m_Mie.m_Y*mie+a.m_Absorption.m_Z*ozone) * static_cast<float>(step);
		}
		return Vector3(std::exp(-opticalDepth.m_X), std::exp(-opticalDepth.m_Y), std::exp(-opticalDepth.m_Z));
	}
}
