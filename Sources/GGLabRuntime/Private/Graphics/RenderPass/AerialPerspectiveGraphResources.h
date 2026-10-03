#pragma once
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"

namespace gglab
{
	inline constexpr const char* AerialPerspectiveResourcesName = "Atmosphere.AerialPerspective.Resources";
	struct RGAerialPerspectiveResources
	{
		RGTextureId m_RadianceAtlas{};
		RGTextureId m_ThroughputAtlas{};
		RGTextureId m_SceneColor{};
		RGTextureId m_Diagnostic{};
		RGBufferId m_ProbeBuffer{};
		PostProcessDebugTap m_DiagnosticTap = PostProcessDebugTap::Count;
	};
}
