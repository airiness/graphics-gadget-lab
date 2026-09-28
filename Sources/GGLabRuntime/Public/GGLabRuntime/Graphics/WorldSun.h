#pragma once

#include "GGLabRuntime/Core/Math/Vector.h"

namespace gglab
{
	// Only a designated directional light uses these units. Local lights remain legacy.
	struct WorldSunSettings
	{
		float m_PerpendicularIlluminanceLux = 120000.0f;
		Vector3 m_Chromaticity = Vector3::One;
		float m_AngularRadiusDegrees = 0.2666f;
	};

	struct ResolvedWorldSun
	{
		// Direction of photon travel, shared by direct lighting and shadows.
		Vector3 m_Direction = -Vector3::UnitY;
		Vector3 m_Chromaticity = Vector3::One;
		float m_PerpendicularIlluminanceLux = 0.0f;
		float m_AngularRadiusRadians = 0.0f;
		float m_ProjectedSolidAngle = 0.0f;
		Vector3 m_DiskRadiance = Vector3::Zero;
		// No atmosphere is active yet: local perpendicular irradiance equals TOA.
		Vector3 m_DirectIlluminance = Vector3::Zero;
	};

	[[nodiscard]] ResolvedWorldSun ResolveWorldSun(
		const WorldSunSettings& settings, const Vector3& direction) noexcept;
}
