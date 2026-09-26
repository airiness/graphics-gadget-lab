#pragma once
#include "Graphics/PostProcess/PostProcessColor.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "Graphics/PostProcess/PostProcessOutput.h"
#include "GGLabRuntime/Graphics/PostProcess/ViewRenderSettings.h"

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
		ResolvedExposureSettings m_Exposure{};
		RGPostProcessInputs m_Inputs{};
		RGBloomResources m_Bloom{};
		RGPostProcessOutputTarget m_Output{};
	};

	inline constexpr const char* PostProcessResourcesName = "RGPostProcessResources";
}
