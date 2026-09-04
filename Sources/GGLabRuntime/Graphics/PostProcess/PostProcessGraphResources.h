#pragma once
#include "Graphics/PostProcess/PostProcessColor.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "Graphics/PostProcess/PostProcessOutput.h"

#include <array>

namespace gglab
{
	struct RGBloomResources
	{
		std::array<RGTextureId, MaxBloomPyramidLevels> m_Pyramid{};
		std::array<RGPostProcessColor, MaxBloomPyramidLevels> m_DownsampledPyramid{};
		uint32_t m_LevelCount = 0;
		RGPostProcessColor m_Prefilter{};
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
