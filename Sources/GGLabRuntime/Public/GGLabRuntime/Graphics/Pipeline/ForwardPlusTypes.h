#pragma once

#include <cstdint>

namespace gglab
{
	inline constexpr uint32_t ForwardPlusTileSize = 16;
	inline constexpr uint32_t ForwardPlusTileLightCapacity = 64;
	inline constexpr uint32_t ForwardPlusGlobalLightCapacity = 4;
	inline constexpr uint32_t ForwardPlusTileCountMask = 0x0000ffffu;
	inline constexpr uint32_t ForwardPlusCullThreadCount =
		ForwardPlusTileSize * ForwardPlusTileSize;

	enum class ForwardPlusFrameStatus : uint8_t
	{
		Disabled,
		Active,
		GlobalLightCapacityExceeded,
		DepthCoverageUnavailable,
		RenderSceneUnavailable,
		NoOpaqueDraws,
	};

	struct ForwardPlusTileGrid
	{
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_TileCountX = 0;
		uint32_t m_TileCountY = 0;
		uint32_t m_TileCount = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Width > 0 && m_Height > 0 &&
				m_TileCountX > 0 && m_TileCountY > 0 &&
				m_TileCount == m_TileCountX * m_TileCountY;
		}
	};

	struct ForwardPlusTileHeader
	{
		uint32_t m_Offset = 0;
		uint32_t m_CountAndFlags = 0;

		[[nodiscard]] uint32_t GetCount() const noexcept
		{
			return m_CountAndFlags & ForwardPlusTileCountMask;
		}
	};
	static_assert(sizeof(ForwardPlusTileHeader) == 8);
}
