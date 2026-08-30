#pragma once
#include "Graphics/RenderGraph/RGResource.h"

#include <cstdint>

namespace gglab
{
	enum class TemporalColorAbi : uint8_t
	{
		LinearRec709SceneReferredV1,
	};

	// Compact semantic state for the color representations currently supported by gglab.
	enum class PostProcessColorState : uint8_t
	{
		SceneLinearRec709,
		DisplayLinearRec709,
		DisplayEncodedSRGB,
	};

	[[nodiscard]] constexpr bool IsTemporalColorCompatible(TemporalColorAbi colorAbi,
		PostProcessColorState colorState, float preExposure) noexcept
	{
		return colorAbi == TemporalColorAbi::LinearRec709SceneReferredV1 &&
			colorState == PostProcessColorState::SceneLinearRec709 && preExposure == 1.0f;
	}

	struct RGPostProcessColor
	{
		RGTextureId m_Texture{};
		PostProcessColorState m_State = PostProcessColorState::SceneLinearRec709;

		// StoredColor = SceneLinearColor * PreExposure. Scene rendering is not
		// pre-exposed yet, so the current path requires this value to remain 1.
		float m_PreExposure = 1.0f;
	};

	struct RGPostProcessInputs
	{
		RGPostProcessColor m_SceneColor{};
	};
}
