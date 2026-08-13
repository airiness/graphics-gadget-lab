#pragma once

#include "Graphics/Pipeline/ForwardPlus.h"
#include "Graphics/RenderGraph/RGResource.h"

#include <array>
#include <memory>

namespace gglab
{
	class ForwardPlusDebugReadback;

	struct RGForwardPlusResources
	{
		RGBufferId m_TileLightHeaders{};
		RGBufferId m_TileLightIndices{};
		RGBufferId m_TileDepthRanges{};
		ForwardPlusTileGrid m_TileGrid{};
		ForwardPlusFrameStatus m_Status = ForwardPlusFrameStatus::Disabled;
		uint32_t m_LightBaseIndex = 0;
		uint32_t m_LightTableCapacity = 0;
		uint32_t m_DirectionalLightCount = 0;
		uint32_t m_LocalLightCount = 0;
		std::array<uint32_t, ForwardPlusTileLightCapacity> m_LightTypesByIndex{};
		std::shared_ptr<ForwardPlusDebugReadback> m_DebugReadback;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_TileLightHeaders.IsValid() && m_TileLightIndices.IsValid() &&
				m_TileGrid.IsValid();
		}

		[[nodiscard]] bool HasGridDiagnostics() const noexcept
		{
			return m_TileDepthRanges.IsValid() && m_DebugReadback != nullptr;
		}
	};

	inline constexpr const char* ForwardPlusResourcesName = "ForwardPlus.Resources";
}
