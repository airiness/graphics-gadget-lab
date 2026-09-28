#pragma once
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
namespace gglab
{
	inline constexpr const char* AtmosphereResourcesName = "Atmosphere.Resources";
	inline constexpr const char* BakeAtmosphereResourcesName = "IBL.Atmosphere.Resources";
	inline constexpr std::array<const char*, 3> BakeAtmosphereLutNames{
		"IBL.Atmosphere.Transmittance", "IBL.Atmosphere.MultipleScattering", "IBL.Atmosphere.SkyView" };
	struct RGAtmosphereResources
	{
		std::array<RGTextureId, 3> m_Luts{};
		AtmosphereDiagnostics m_Diagnostics{};
	};
}
