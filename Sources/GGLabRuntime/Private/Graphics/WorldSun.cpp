#include "GGLabRuntime/Graphics/WorldSun.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Core/Math/Matrix.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gglab
{
	namespace
	{
		constexpr float MaximumIlluminanceLux = 1000000.0f;
		constexpr float MinimumAngularRadiusDegrees = 0.01f;
		constexpr float MaximumAngularRadiusDegrees = 5.0f;
		constexpr float MinimumChromaticityLuminance = 1.0e-8f;

		float Luminance(const Vector3& value) noexcept
		{
			return 0.2126f * value.m_X + 0.7152f * value.m_Y + 0.0722f * value.m_Z;
		}
	}

	bool ValidateWorldSunSettings(const WorldSunSettings& settings) noexcept
	{
		return math::IsInClosedRange(settings.m_PerpendicularIlluminanceLux, 0.0f, MaximumIlluminanceLux) &&
			math::IsInClosedRange(settings.m_Chromaticity, 0.0f, 1.0f) &&
			Luminance(settings.m_Chromaticity) > MinimumChromaticityLuminance &&
			math::IsInClosedRange(settings.m_AngularRadiusDegrees,
				MinimumAngularRadiusDegrees, MaximumAngularRadiusDegrees);
	}

	Vector3 ResolveWorldSunDirection(const Quaternion& rotation) noexcept
	{
		Vector3 direction = math::TransformDirection(Vector3::Forward, math::CreateFromQuaternion(rotation));
		if (!math::IsFinite(direction.LengthSquared()) || direction.LengthSquared() <= 1.0e-8f)
		{
			return -Vector3::UnitY;
		}
		direction.Normalize();
		return direction;
	}

	ResolvedWorldSun ResolveWorldSun(const WorldSunSettings& settings, const Vector3& direction) noexcept
	{
		ResolvedWorldSun result{};
		const float lengthSquared = direction.LengthSquared();
		if (std::isfinite(lengthSquared) && lengthSquared > 1.0e-8f)
		{
			result.m_Direction = direction / std::sqrt(lengthSquared);
		}
		result.m_PerpendicularIlluminanceLux = std::isfinite(settings.m_PerpendicularIlluminanceLux) ?
			std::clamp(settings.m_PerpendicularIlluminanceLux, 0.0f, MaximumIlluminanceLux) :
			0.0f;
		const auto channel = [](float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; };
		Vector3 chromaticity(channel(settings.m_Chromaticity.m_X),
			channel(settings.m_Chromaticity.m_Y), channel(settings.m_Chromaticity.m_Z));
		const float luminance = Luminance(chromaticity);
		result.m_Chromaticity = luminance > MinimumChromaticityLuminance ? chromaticity / luminance : Vector3::One;
		const float radius = std::isfinite(settings.m_AngularRadiusDegrees) ?
			std::clamp(settings.m_AngularRadiusDegrees, MinimumAngularRadiusDegrees, MaximumAngularRadiusDegrees) :
			0.2666f;
		result.m_AngularRadiusRadians = radius * (std::numbers::pi_v<float> / 180.0f);
		const float sine = std::sin(result.m_AngularRadiusRadians);
		// Integral of cos(theta) over a uniform disk, not the unprojected solid angle.
		result.m_ProjectedSolidAngle = std::numbers::pi_v<float> * sine * sine;
		result.m_DirectIlluminance = result.m_Chromaticity * result.m_PerpendicularIlluminanceLux;
		result.m_DiskRadiance = result.m_DirectIlluminance / result.m_ProjectedSolidAngle;
		return result;
	}
}
