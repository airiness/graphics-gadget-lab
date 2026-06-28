#pragma once
#include "Graphics/PipelineCache.h"
#include "Graphics/RHI/RHIBindingLayout.h"
#include "Graphics/RHI/RHIPipeline.h"

namespace gglab
{
	[[nodiscard]] RHIVertexInputLayoutDesc BuildRHIVertexInputLayoutDesc(InputLayoutID inputLayoutId) noexcept;
	[[nodiscard]] RHIGraphicsPipelineDesc BuildRHIGraphicsPipelineDesc(
		const GraphicsPipelineRecipe& recipe) noexcept;
}
