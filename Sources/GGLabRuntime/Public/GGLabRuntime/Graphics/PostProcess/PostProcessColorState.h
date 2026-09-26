#pragma once

#include <cstdint>

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

	// Pre-exposed history requires scale conversion before its ABI can be accepted.
	[[nodiscard]] constexpr bool IsTemporalColorCompatible(TemporalColorAbi colorAbi,
		PostProcessColorState colorState, float preExposure) noexcept
	{
		return colorAbi == TemporalColorAbi::LinearRec709SceneReferredV1 &&
			colorState == PostProcessColorState::SceneLinearRec709 && preExposure == 1.0f;
	}

	inline constexpr float SceneColorStoragePreExposureV1 = 1.0f;
}
