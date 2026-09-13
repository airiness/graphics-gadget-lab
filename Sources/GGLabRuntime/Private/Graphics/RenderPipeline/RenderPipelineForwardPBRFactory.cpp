#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

namespace gglab
{
	std::unique_ptr<RenderPipelineBase> CreateRenderPipelineForwardPBR(
		RenderPipelineForwardPBRCreateInfo createInfo) noexcept
	{
		return std::make_unique<RenderPipelineForwardPBR>(RenderPipelineForwardPBR::CreateInfo{
			.m_ForwardPlusDebugReadback = std::move(createInfo.m_ForwardPlusDebugReadback),
			.m_SceneExtension = std::move(createInfo.m_SceneExtension),
		});
	}
}
