#pragma once
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"

#include <array>
#include <cstddef>

namespace gglab
{
	inline constexpr const char* PostProcessPreviewResourcesName = "PostProcess.Preview.Outputs";

	// GUI consumers declare sampled reads of these Runtime-owned preview outputs.
	struct RGPostProcessPreviewResources
	{
		std::array<RGTextureId, static_cast<size_t>(PostProcessPreviewChannel::Count)> m_Textures{};
	};
}
