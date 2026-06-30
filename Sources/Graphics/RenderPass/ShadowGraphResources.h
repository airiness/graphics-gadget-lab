#pragma once
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/ShadowSettings.h"

namespace gglab
{
	struct RGShadowResources
	{
		RGTextureId m_DirectionalShadowMap{};
		RGTextureId m_DirectionalShadowMapPreview{};
		uint32_t m_ShadowMapSize = DefaultDirectionalShadowMapSize;
		uint32_t m_ShadowMapPreviewSize = DefaultDirectionalShadowMapPreviewSize;
	};

	inline constexpr const char* ShadowResourcesName = "RGShadowResources";
}
