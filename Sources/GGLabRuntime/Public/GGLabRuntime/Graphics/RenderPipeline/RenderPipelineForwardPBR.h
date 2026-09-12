#pragma once
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineSceneExtensionBase.h"

#include <memory>

namespace gglab
{
	class ForwardPlusDebugReadback;

	// Public authoring inputs for the default content render pipeline. The
	// concrete pipeline and its pass roster stay Runtime-internal.
	struct RenderPipelineForwardPBRCreateInfo
	{
		std::shared_ptr<ForwardPlusDebugReadback> m_ForwardPlusDebugReadback;
		std::unique_ptr<RenderPipelineSceneExtensionBase> m_SceneExtension;
	};

	[[nodiscard]] std::unique_ptr<RenderPipelineBase> CreateRenderPipelineForwardPBR(
		RenderPipelineForwardPBRCreateInfo createInfo = {}) noexcept;
}
