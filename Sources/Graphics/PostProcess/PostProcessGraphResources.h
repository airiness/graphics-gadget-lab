#pragma once
#include "Graphics/PostProcess/PostProcessColor.h"
#include "Graphics/PostProcess/PostProcessOutput.h"

namespace gglab
{
	struct RGPostProcessResources
	{
		RGPostProcessInputs m_Inputs{};
		RGPostProcessOutputTarget m_Output{};
	};

	inline constexpr const char* PostProcessResourcesName = "RGPostProcessResources";
}
