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
}
