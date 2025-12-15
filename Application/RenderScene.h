#pragma once
#include <cstdint>
#include "GraphicsTypes.h"
#include "GPUStructures.h"

namespace gglab
{
	struct DrawItem
	{
		MeshId m_MeshId{};
		MaterialId m_MaterialId{};
		uint32_t m_ObjectOffset = 0;
	};

	struct RenderScene
	{
		uint32_t m_ObjectBaseIndex = 0;
		uint32_t m_ObjectCount = 0;

		uint32_t m_MaterialBaseIndex = 0;
		uint32_t m_MaterialCount = 0;

		LightGPU m_MainLight{};
		std::vector<DrawItem> m_DrawItems;
	};
}