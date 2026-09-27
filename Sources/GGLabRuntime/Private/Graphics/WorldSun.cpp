#include "GGLabRuntime/Graphics/WorldSun.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gglab
{
	ResolvedWorldSun ResolveWorldSun(const WorldSunSettings& settings, const Vector3& direction) noexcept
	{
		ResolvedWorldSun result{};
		const float lengthSquared = direction.LengthSquared();
		if (std::isfinite(lengthSquared) && lengthSquared > 1.0e-8f)
		{
			result.m_Direction = direction / std::sqrt(lengthSquared);
		}
		result.m_PerpendicularIlluminanceLux = std::isfinite(settings.m_PerpendicularIlluminanceLux)
			? std::clamp(settings.m_PerpendicularIlluminanceLux, 0.0f, 1000000.0f) : 0.0f;
		const auto channel = [](float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; };
		Vector3 chromaticity(channel(settings.m_Chromaticity.m_X),
			channel(settings.m_Chromaticity.m_Y), channel(settings.m_Chromaticity.m_Z));
		const float luminance = 0.2126f * chromaticity.m_X + 0.7152f * chromaticity.m_Y + 0.0722f * chromaticity.m_Z;
		result.m_Chromaticity = luminance > 1.0e-8f ? chromaticity / luminance : Vector3::One;
		const float radius = std::isfinite(settings.m_AngularRadiusDegrees)
			? std::clamp(settings.m_AngularRadiusDegrees, 0.01f, 5.0f) : 0.2666f;
		result.m_AngularRadiusRadians = radius * (std::numbers::pi_v<float> / 180.0f);
		const float sine = std::sin(result.m_AngularRadiusRadians);
		// Integral of cos(theta) over a uniform disk, not the unprojected solid angle.
		result.m_ProjectedSolidAngle = std::numbers::pi_v<float> * sine * sine;
		result.m_DirectIlluminance = result.m_Chromaticity * result.m_PerpendicularIlluminanceLux;
		result.m_DiskRadiance = result.m_DirectIlluminance / result.m_ProjectedSolidAngle;
		return result;
	}
}
