#pragma once
#include "Graphics/RenderGraph/RGResource.h"

namespace gglab
{
	struct RGIBLPreviewResources
	{
		RGTextureId m_EnvironmentCubemapPreview{};
		RGTextureId m_PrefilteredSpecularCubemapPreview{};
	};

	inline constexpr const char* IBLPreviewResourcesName = "RGIBLPreviewResources";
}
