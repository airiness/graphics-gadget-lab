#pragma once
#include "GGLabRuntime/Graphics/LightingProfile.h"

#include <array>

namespace gglab
{
	inline const std::array CoastalSceneLightingProfiles
	{
		WorldLightingProfile{ .m_Id = "coastal.daylight",
			.m_Inputs = { .m_SunDirection = Vector3(-0.6840816f, -0.3907311f, 0.6159450f) },
			.m_RecommendedEV100 = 15.0f },
	};
}
