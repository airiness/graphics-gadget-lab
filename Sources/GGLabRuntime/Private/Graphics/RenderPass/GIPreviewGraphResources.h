#pragma once
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewDiagnostics.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"

#include <array>

namespace gglab
{
	inline constexpr const char* GIPreviewResourcesName = "GI.Preview.Sources";

	// Producers publish graph identities, never borrowed native texture views.
	// Sun/environment alpha has filter semantics; use irradiance for validity.
	struct RGGIPreviewResources
	{
		std::array<RGTextureId, 4> m_Diffuse{};
		RGTextureId m_Visibility;
		RGTextureId m_Activity;
		GIPreviewSnapshot m_DiffuseSnapshot{};
		GIPreviewSnapshot m_VisibilitySnapshot{};
	};
}
