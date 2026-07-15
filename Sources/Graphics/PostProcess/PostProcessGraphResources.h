#pragma once
#include "Graphics/PostProcess/PostProcessColor.h"
#include "Graphics/PostProcess/PostProcessOutput.h"

#include <array>

namespace gglab
{
	inline constexpr uint32_t MaxBloomPyramidLevels = 8;

	struct RGBloomResources
	{
		std::array<RGTextureId, MaxBloomPyramidLevels> m_Pyramid{};
		uint32_t m_LevelCount = 0;
		RGPostProcessColor m_Result{};
	};

	struct RGPostProcessResources
	{
		RGPostProcessInputs m_Inputs{};
		RGBloomResources m_Bloom{};
		RGPostProcessOutputTarget m_Output{};
	};

	inline constexpr const char* PostProcessResourcesName = "RGPostProcessResources";
}
