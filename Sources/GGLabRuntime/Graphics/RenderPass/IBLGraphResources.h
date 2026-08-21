#pragma once
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/RenderGraph/RGResourceUtils.h"

namespace gglab
{
	struct RGIBLResources
	{
		RGTextureId m_EnvironmentCubemap{};
		RGTextureId m_IrradianceCubemap{};
		RGTextureId m_PrefilteredSpecularCubemap{};
		RGTextureId m_BrdfLut{};

		RGTextureId m_BakeEnvironmentCubemap{};
		RGTextureId m_BakeIrradianceCubemap{};
		RGTextureId m_BakePrefilteredSpecularCubemap{};
		RGTextureId m_BakeBrdfLut{};
	};

	inline constexpr const char* IBLResourcesName = "RGIBLResources";

	struct RGIBLPreviewResources
	{
		RGTextureId m_EnvironmentCubemapPreview{};
		RGTextureId m_IrradianceCubemapPreview{};
		RGTextureId m_PrefilteredSpecularCubemapPreview{};
	};

	inline constexpr const char* IBLPreviewResourcesName = "RGIBLPreviewResources";
}
