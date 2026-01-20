#pragma once
#include <cstdint>
#include "GPUStructures.h"

namespace gglab
{
	struct RenderScene
	{
		uint32_t m_ObjectBaseIndex = 0;
		uint32_t m_ObjectCount = 0;

		uint32_t m_MaterialBaseIndex = 0;
		uint32_t m_MaterialCount = 0;

		LightGPU m_MainLight{};
	};
}