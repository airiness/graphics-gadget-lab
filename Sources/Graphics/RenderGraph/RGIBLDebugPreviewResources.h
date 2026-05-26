#pragma once
#include "Graphics/RenderGraph/RGResource.h"

namespace gglab
{
	struct RGIBLDebugPreviewResources
	{
		RGTextureId m_EnvironmentCubemapPreview{};
	};

	inline constexpr const char* IBLDebugPreviewResourcesName = "RGIBLDebugPreviewResources";
}
