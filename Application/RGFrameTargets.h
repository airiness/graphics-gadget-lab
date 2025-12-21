#pragma once
#include "RGResource.h"

namespace gglab
{
	struct RGFrameTargets
	{
		RGTextureId m_BackBuffer{};
		RGTextureId m_Depth{};

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		ViewKey m_BackBufferRTVKey{};
	};
	inline constexpr const char* MainViewName = "MainView";
}