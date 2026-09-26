#pragma once
#include "GGLabRuntime/Graphics/PostProcess/PostProcessColorState.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"

#include <cstdint>

namespace gglab
{
	struct RGPostProcessColor
	{
		RGTextureId m_Texture{};
		PostProcessColorState m_State = PostProcessColorState::SceneLinearRec709;

		// StoredColor = SceneLinearColor * PreExposure. Scene rendering is not
		// pre-exposed yet, so the current path requires this value to remain 1.
		float m_PreExposure = SceneColorStoragePreExposureV1;
	};

	struct RGPostProcessInputs
	{
		RGPostProcessColor m_SceneColor{};
	};
}
