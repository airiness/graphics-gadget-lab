#pragma once
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderPipeline/DepthCoverageFramePlan.h"
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

	// Display-domain depth that post-temporal composition depth-tests against and post-TAA
	// scene extensions write. At native resolution it aliases the render-domain scene
	// depth; temporal upscaling makes the resolve produce it.
	struct RGDisplayDepthResources
	{
		RGTextureId m_Texture{};
		RHITextureViewDesc m_DsvDesc{};
		RHITextureViewDesc m_SrvDesc{};
		DepthConvention m_Convention = DepthConvention::Reversed;
	};

	inline constexpr const char* DisplayDepthResourcesName = "RGDisplayDepthResources";

	inline constexpr const char* DepthCoverageFramePlanName = "DepthCoverageFramePlan";
}
