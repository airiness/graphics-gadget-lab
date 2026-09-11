#pragma once
#include "GGLabRuntime/Graphics/PostProcess/PostProcessColorState.h"
#include "Graphics/RenderGraph/RGResource.h"

#include <cstdint>

namespace gglab
{
	enum class TemporalColorAbi : uint8_t
	{
		LinearRec709SceneReferredV1,
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
