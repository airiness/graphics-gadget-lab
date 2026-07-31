#pragma once

#include "Graphics/Pipeline/ForwardPlus.h"
#include "Graphics/RenderGraph/RGResource.h"

namespace gglab
{
	struct RGForwardPlusResources
	{
		RGBufferId m_TileLightHeaders{};
		RGBufferId m_TileLightIndices{};
		ForwardPlusTileGrid m_TileGrid{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_TileLightHeaders.IsValid() && m_TileLightIndices.IsValid() &&
				m_TileGrid.IsValid();
		}
	};

	inline constexpr const char* ForwardPlusResourcesName = "ForwardPlus.Resources";
}
