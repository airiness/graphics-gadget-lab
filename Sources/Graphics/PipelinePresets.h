#pragma once
#include <cstdint>

namespace gglab
{
	enum class RasterizerPreset : uint8_t
	{
		Default,
		Wireframe,
		TwoSided,
		CullFront,
	};

	enum class BlendPreset : uint8_t
	{
		Default,
		AlphaBlend,
		Additive,
		PremultipliedAlpha,
		ColorWriteDisable,
	};

	enum class DepthPreset : uint8_t
	{
		Default,
		DepthReadOnly,
		DepthDisabled,
		ReverseZ,
		ReverseZReadOnly,
	};
}
