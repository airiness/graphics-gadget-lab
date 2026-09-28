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

		// StoredColor = SceneLinearColor * PreExposure, applied before FP16 storage.
		float m_PreExposure = SceneColorStoragePreExposureV1;
	};

	struct RGPostProcessInputs
	{
		RGPostProcessColor m_SceneColor{};
	};
}
