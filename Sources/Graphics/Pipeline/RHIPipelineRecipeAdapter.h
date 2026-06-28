#pragma once
#include "Graphics/Pipeline/PipelineCache.h"

namespace gglab
{
	[[nodiscard]] RHIVertexInputLayoutDesc BuildRHIVertexInputLayoutDesc(InputLayoutID inputLayoutId) noexcept;
	[[nodiscard]] RHIGraphicsPipelineDesc BuildRHIGraphicsPipelineDesc(
		const GraphicsPipelineRecipe& recipe) noexcept;
}
