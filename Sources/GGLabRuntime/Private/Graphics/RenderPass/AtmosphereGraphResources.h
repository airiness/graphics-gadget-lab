#pragma once
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
namespace gglab
{
	inline constexpr const char* AtmosphereResourcesName = "Atmosphere.Resources";
	struct RGAtmosphereResources
	{
		std::array<RGTextureId, 3> m_Luts{};
		AtmosphereDiagnostics m_Diagnostics{};
	};
}
