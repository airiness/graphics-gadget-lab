#pragma once
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/ScreenSpace/ScreenSpaceTypes.h"

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
		RGTextureId m_SceneDepth{};
		RHIFormat m_SceneDepthDsvFormat = RHIFormat::Unknown;
		RHIFormat m_SceneDepthSrvFormat = RHIFormat::Unknown;
		DepthConvention m_DepthConvention = DepthConvention::Standard;
	};
}
