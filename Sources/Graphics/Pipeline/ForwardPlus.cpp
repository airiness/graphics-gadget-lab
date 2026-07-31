#include "Core/Precompiled.h"
#include "Graphics/Pipeline/ForwardPlus.h"

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
