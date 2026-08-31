#pragma once
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/RenderPipeline/DepthCoverageFramePlan.h"
#include "GGLabRuntime/Graphics/ScreenSpace/ScreenSpaceTypes.h"

namespace gglab
{
	struct RGSceneDepthResources
	{
		RGTextureId m_Texture{};
		RHITextureViewDesc m_DsvDesc{};
		RHITextureViewDesc m_SrvDesc{};
		DepthConvention m_Convention = DepthConvention::Reversed;
	};

	inline constexpr const char* SceneDepthResourcesName = "RGSceneDepthResources";

	inline constexpr const char* DepthCoverageFramePlanName = "DepthCoverageFramePlan";
}
