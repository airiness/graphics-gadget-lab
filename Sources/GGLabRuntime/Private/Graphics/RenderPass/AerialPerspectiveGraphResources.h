#pragma once
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/Pipeline/ViewRenderFeatureStatus.h"

namespace gglab
{
	inline constexpr const char* AerialPerspectiveResourcesName = "Atmosphere.AerialPerspective.Resources";
	inline constexpr const char* AerialPerspectiveFrameStatusName = "Atmosphere.AerialPerspective.FrameStatus";
	struct RGAerialPerspectiveFrameStatus
	{
		ViewRenderFeatureStatus m_Status{};
		ViewRenderFeatureStatus m_ProbeStatus{};
	};

	struct RGAerialPerspectiveResources
	{
		RGTextureId m_RadianceAtlas{};
		RGTextureId m_ThroughputAtlas{};
		RGTextureId m_Diagnostic{};
		RGBufferId m_ProbeBuffer{};
		PostProcessDebugTap m_DiagnosticTap = PostProcessDebugTap::Count;
	};
}
