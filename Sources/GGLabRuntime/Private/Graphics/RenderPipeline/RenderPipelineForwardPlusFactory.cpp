#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPlus.h"

namespace gglab
{
	std::unique_ptr<RenderPipelineBase> CreateRenderPipelineForwardPlus(
		RenderPipelineForwardPlusCreateInfo createInfo) noexcept
	{
		return std::make_unique<RenderPipelineForwardPlus>(RenderPipelineForwardPlus::CreateInfo{
			.m_ForwardPlusDebugReadback = std::move(createInfo.m_ForwardPlusDebugReadback),
			.m_SceneExtension = std::move(createInfo.m_SceneExtension),
		});
	}
}
