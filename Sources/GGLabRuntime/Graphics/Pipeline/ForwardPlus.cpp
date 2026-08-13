#include "Graphics/Pipeline/ForwardPlus.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace gglab
{
	ForwardPlusTileGrid MakeForwardPlusTileGrid(uint32_t width, uint32_t height) noexcept
	{
		if (width == 0 || height == 0)
		{
			return {};
		}

		const uint32_t tileCountX = (width + ForwardPlusTileSize - 1) / ForwardPlusTileSize;
		const uint32_t tileCountY = (height + ForwardPlusTileSize - 1) / ForwardPlusTileSize;
		const uint64_t tileCount =
			static_cast<uint64_t>(tileCountX) * static_cast<uint64_t>(tileCountY);
		if (tileCount > std::numeric_limits<uint32_t>::max())
		{
			return {};
		}

		return {
			.m_Width = width,
			.m_Height = height,
			.m_TileCountX = tileCountX,
			.m_TileCountY = tileCountY,
			.m_TileCount = static_cast<uint32_t>(tileCount),
		};
	}

	ForwardPlusGridMetrics BuildForwardPlusGridMetrics(const ForwardPlusTileGrid& tileGrid,
		std::span<const ForwardPlusTileHeader> headers,
		std::span<const ForwardPlusTileDepthRange> depthRanges) noexcept
	{
		if (!tileGrid.IsValid() || headers.size() != tileGrid.m_TileCount ||
			depthRanges.size() != tileGrid.m_TileCount)
		{
			return {};
		}

		ForwardPlusGridMetrics metrics{};
		float minimumViewZ = std::numeric_limits<float>::max();
		for (uint32_t tileIndex = 0; tileIndex < tileGrid.m_TileCount; ++tileIndex)
		{
			const ForwardPlusTileHeader& header = headers[tileIndex];
			const uint32_t lightCount = header.GetCount();
			metrics.m_NonEmptyLightListTileCount += lightCount > 0 ? 1u : 0u;
			metrics.m_TotalLightReferences += lightCount;
			metrics.m_MaxLightsPerTile = std::max(metrics.m_MaxLightsPerTile, lightCount);
			metrics.m_OverflowTileCount +=
				lightCount > ForwardPlusTileLightCapacity ||
				(header.m_CountAndFlags & ~ForwardPlusTileCountMask) != 0
				? 1u
				: 0u;

			const ForwardPlusTileDepthRange& depthRange = depthRanges[tileIndex];
			if (depthRange.IsValid())
			{
				minimumViewZ = std::min(minimumViewZ, depthRange.m_MinViewZ);
				metrics.m_MaxViewZ = std::max(metrics.m_MaxViewZ, depthRange.m_MaxViewZ);
			}
		}

		metrics.m_EmptyLightListTileCount =
			tileGrid.m_TileCount - metrics.m_NonEmptyLightListTileCount;
		metrics.m_AverageLightsPerTile =
			static_cast<double>(metrics.m_TotalLightReferences) / tileGrid.m_TileCount;
		metrics.m_MinViewZ = minimumViewZ == std::numeric_limits<float>::max()
			? 0.0f
			: minimumViewZ;
		metrics.m_IsValid = true;
		return metrics;
	}

	std::vector<uint32_t> BuildStableForwardPlusLightList(
		std::span<const uint8_t> lightHits, uint32_t simulatedWaveSize)
	{
		if ((simulatedWaveSize != 32 && simulatedWaveSize != 64) ||
			lightHits.size() > ForwardPlusTileLightCapacity)
		{
			return {};
		}

		std::vector<uint32_t> waveCounts(
			(lightHits.size() + simulatedWaveSize - 1) / simulatedWaveSize);
		for (size_t lightIndex = 0; lightIndex < lightHits.size(); ++lightIndex)
		{
			if (lightHits[lightIndex] != 0)
			{
				++waveCounts[lightIndex / simulatedWaveSize];
			}
		}

		std::vector<uint32_t> waveOffsets(waveCounts.size());
		uint32_t runningOffset = 0;
		for (size_t waveIndex = 0; waveIndex < waveCounts.size(); ++waveIndex)
		{
			waveOffsets[waveIndex] = runningOffset;
			runningOffset += waveCounts[waveIndex];
		}

		std::vector<uint32_t> result(runningOffset);
		std::vector<uint32_t> waveLocalOffsets(waveCounts.size());
		for (size_t lightIndex = 0; lightIndex < lightHits.size(); ++lightIndex)
		{
			if (lightHits[lightIndex] == 0)
			{
				continue;
			}
			const size_t waveIndex = lightIndex / simulatedWaveSize;
			const uint32_t outputIndex = waveOffsets[waveIndex] + waveLocalOffsets[waveIndex]++;
			result[outputIndex] = static_cast<uint32_t>(lightIndex);
		}
		return result;
	}
}
