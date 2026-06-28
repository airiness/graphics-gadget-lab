#pragma once
#include "Graphics/RHI/RHIBindingLayout.h"
#include "Graphics/RHI/RHIPipeline.h"
#include "Graphics/ShaderTypes.h"

#include <cstddef>

namespace gglab
{
	struct RHIGraphicsPipelineCreateInfo
	{
		RHIGraphicsPipelineDesc m_Desc{};
		ShaderBytecode m_VertexShader{};
		ShaderBytecode m_PixelShader{};
		ShaderBytecode m_DomainShader{};
		ShaderBytecode m_HullShader{};
		ShaderBytecode m_GeometryShader{};
	};

	struct RHIComputePipelineCreateInfo
	{
		RHIComputePipelineDesc m_Desc{};
		ShaderBytecode m_ComputeShader{};
	};

	class RHIPipelineSystem
	{
	public:
		virtual ~RHIPipelineSystem() = default;

		[[nodiscard]] virtual RHIBindingLayoutHandle CreateBindingLayout(
			const RHIBindingLayoutDesc& desc) noexcept = 0;
		[[nodiscard]] virtual RHIPipelineHandle CreateGraphicsPipeline(
			const RHIGraphicsPipelineCreateInfo& createInfo) noexcept = 0;
		[[nodiscard]] virtual RHIPipelineHandle CreateComputePipeline(
			const RHIComputePipelineCreateInfo& createInfo) noexcept = 0;
		[[nodiscard]] virtual bool IsAlive(RHIBindingLayoutHandle layout) const noexcept = 0;
		[[nodiscard]] virtual bool IsAlive(RHIPipelineHandle pipeline) const noexcept = 0;
		[[nodiscard]] virtual uint64_t GetRevision() const noexcept = 0;
		virtual void Clear() noexcept = 0;
	};
}
