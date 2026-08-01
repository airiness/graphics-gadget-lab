#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

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

	struct ForwardPlusTileDepthRange
	{
		float m_MinViewZ = 0.0f;
		float m_MaxViewZ = 0.0f;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_MinViewZ > 0.0f && m_MaxViewZ >= m_MinViewZ;
		}
	};
	static_assert(sizeof(ForwardPlusTileDepthRange) == 8);

	struct ForwardPlusGridMetrics
	{
		uint32_t m_ActiveTileCount = 0;
		uint32_t m_EmptyTileCount = 0;
		uint64_t m_TotalLightReferences = 0;
		double m_AverageLightsPerTile = 0.0;
		uint32_t m_MaxLightsPerTile = 0;
		uint32_t m_OverflowTileCount = 0;
		float m_MinViewZ = 0.0f;
		float m_MaxViewZ = 0.0f;
		bool m_IsValid = false;
	};

	[[nodiscard]] ForwardPlusTileGrid MakeForwardPlusTileGrid(
		uint32_t width, uint32_t height) noexcept;

	[[nodiscard]] constexpr uint32_t GetForwardPlusTileOffset(uint32_t tileIndex) noexcept
	{
		return tileIndex * ForwardPlusTileLightCapacity;
	}

	[[nodiscard]] constexpr bool IsForwardPlusGlobalLightCountSupported(
		uint32_t lightCount) noexcept
	{
		return lightCount <= ForwardPlusGlobalLightCapacity;
	}

	inline void SortForwardPlusGlobalLightIndices(std::span<uint32_t> lightIndices) noexcept
	{
		std::ranges::sort(lightIndices);
	}

	[[nodiscard]] std::vector<uint32_t> BuildStableForwardPlusLightList(
		std::span<const uint8_t> lightHits, uint32_t simulatedWaveSize);

	[[nodiscard]] ForwardPlusGridMetrics BuildForwardPlusGridMetrics(
		const ForwardPlusTileGrid& tileGrid,
		std::span<const ForwardPlusTileHeader> headers,
		std::span<const ForwardPlusTileDepthRange> depthRanges) noexcept;
}
