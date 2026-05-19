#pragma once
#include "Graphics/RenderGraph/RGResource.h"

namespace gglab
{
	struct RGIBLResources
	{
		RGTextureId m_EnvironmentCubemap{};
		RGTextureId m_IrradianceCubemap{};
		RGTextureId m_PrefilteredSpecularCubemap{};
		RGTextureId m_BrdfLut{};
	};

	inline constexpr const char* IBLResourcesName = "RGIBLResources";
}
