#pragma once

#include "GGLabRuntime/Graphics/IBLBakeConfig.h"
#include <cstdint>

namespace gglab
{
	enum class EnvironmentBackgroundMode : uint8_t
	{
		TextureEnvironment,
		PhysicalAtmospherePreview,
		PhysicalSky,
	};

	struct EnvironmentLightingSettings
	{
		float m_Intensity = 1.0f;
		float m_RotationRadians = 0.0f;
		IBLQualityPreset m_QualityPreset = IBLQualityPreset::Medium;
		IBLBakeConfig m_BakeConfig = GetIBLBakeConfig(IBLQualityPreset::Medium);
		bool m_EnableSkybox = true;
		EnvironmentBackgroundMode m_BackgroundMode = EnvironmentBackgroundMode::TextureEnvironment;
	};
}
