#pragma once
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewDiagnostics.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"

namespace gglab
{
	inline constexpr const char* GIPreviewResourcesName = "GI.Preview.Sources";

	// Producers publish graph identities, never borrowed native texture views.
	// Sun/environment alpha has filter semantics; use irradiance for validity.
	struct RGGIPreviewResources
	{
		RGTextureId m_Irradiance;
		RGTextureId m_LambertRadiance;
		RGTextureId m_SunIrradiance;
		RGTextureId m_EnvironmentIrradiance;
		RGTextureId m_Visibility;
		RGTextureId m_Activity;
		GIPreviewSnapshot m_DiffuseSnapshot{};
		GIPreviewSnapshot m_VisibilitySnapshot{};
	};
}
