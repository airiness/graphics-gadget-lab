#pragma once
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineSceneExtensionBase.h"

#include <memory>

namespace gglab
{
	class ForwardPlusDebugReadback;

	// Public authoring inputs for the default content render pipeline. The
	// concrete pipeline and its pass roster stay Runtime-internal.
	struct RenderPipelineForwardPlusCreateInfo
	{
		// Lab-owned Forward+ instrumentation. Supplying it composes tile readback and
		// the HDR-diff validation recipe; production compositions leave it empty.
		std::shared_ptr<ForwardPlusDebugReadback> m_ForwardPlusDebugReadback;
		std::unique_ptr<RenderPipelineSceneExtensionBase> m_SceneExtension;
		// Runtime-owned receiver diagnostics before the temporal boundary. Requires
		// Native, unjittered rendering with TAA off; ordinary compositions leave this off.
		bool m_EnableGIReceiver = false;
	};

	[[nodiscard]] std::unique_ptr<RenderPipelineBase> CreateRenderPipelineForwardPlus(
		RenderPipelineForwardPlusCreateInfo createInfo = {}) noexcept;
}
