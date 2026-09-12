#pragma once
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Shader/ShaderTypes.h"

namespace gglab
{
	[[nodiscard]] RHIVertexInputLayoutDesc BuildRHIVertexInputLayoutDesc(
		InputLayoutID inputLayoutId, ShaderBinaryFormat vertexFormat = ShaderBinaryFormat::Dxil) noexcept;
	[[nodiscard]] RHIGraphicsPipelineDesc BuildRHIGraphicsPipelineDesc(
		const GraphicsPhysicalPipelineKey& recipe,
		ShaderBinaryFormat vertexFormat = ShaderBinaryFormat::Dxil) noexcept;
}
