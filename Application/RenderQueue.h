#pragma once
#include "GraphicsTypes.h"

#include <cstdint>
#include <vector>

namespace gglab
{
	struct DrawItem
	{
		MeshID m_MeshId{};
		MaterialID m_MaterialId{};
		uint32_t m_ObjectOffset = 0;
	};

	struct DrawRange
	{
		uint32_t m_Start = 0;
		uint32_t m_Count = 0;
	};

	struct RenderQueue
	{
		std::vector<DrawItem> m_DrawItems;
	};
}