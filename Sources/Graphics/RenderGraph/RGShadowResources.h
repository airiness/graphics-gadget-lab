#pragma once
#include "Graphics/RenderGraph/RGResource.h"

namespace gglab
{
	inline constexpr uint32_t DefaultDirectionalShadowMapSize = 2048;

	struct RGShadowResources
	{
		RGTextureId m_DirectionalShadowMap{};
		uint32_t m_ShadowMapSize = DefaultDirectionalShadowMapSize;
	};

	inline constexpr const char* ShadowResourcesName = "RGShadowResources";
}
