#pragma once

#include <cstdint>
#include <limits>

namespace gglab
{
	// Compact semantic state for the color representations currently supported by gglab.
	enum class PostProcessColorState : uint8_t
	{
		SceneLinearRec709,
		DisplayLinearRec709,
		DisplayEncodedSRGB,
	};

	enum class TemporalColorAbi : uint8_t
	{
		LinearRec709SceneReferredV1,
		LinearRec709PreExposedV2,
	};

	inline constexpr TemporalColorAbi ActiveTemporalColorAbi =
		TemporalColorAbi::LinearRec709PreExposedV2;

	[[nodiscard]] constexpr bool IsValidPreExposure(float preExposure) noexcept
	{
		return preExposure > 0.0f && preExposure <= std::numeric_limits<float>::max();
	}

	[[nodiscard]] constexpr bool IsTemporalColorCompatible(TemporalColorAbi colorAbi,
		PostProcessColorState colorState, float preExposure) noexcept
	{
		return colorState == PostProcessColorState::SceneLinearRec709 &&
			((colorAbi == TemporalColorAbi::LinearRec709SceneReferredV1 && preExposure == 1.0f) ||
				(colorAbi == TemporalColorAbi::LinearRec709PreExposedV2 && IsValidPreExposure(preExposure)));
	}

	inline constexpr float SceneColorStoragePreExposureV1 = 1.0f;
}
