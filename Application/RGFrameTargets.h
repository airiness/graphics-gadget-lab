#pragma once
#include "RGResource.h"

namespace gglab
{
	struct RGFrameTargets
	{
		RGTextureId m_BackBuffer{};
		RGTextureId m_Depth{};
	};
	inline constexpr const char* MainViewName = "MainView";
}