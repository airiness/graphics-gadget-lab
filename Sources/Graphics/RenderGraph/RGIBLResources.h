#pragma once
#include "Graphics/RenderGraph/RGResource.h"

namespace gglab
{
	struct RGIBLResources
	{
		RGTextureId m_BrdfLut{};
	};

	inline constexpr const char* IBLResourcesName = "RGIBLResources";
}