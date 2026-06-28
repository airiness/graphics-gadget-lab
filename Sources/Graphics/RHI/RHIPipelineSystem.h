#pragma once
#include "Graphics/RHI/RHIBindingLayout.h"
#include "Graphics/RHI/RHIPipeline.h"

#include <cstddef>

namespace gglab
{
	struct RHIShaderBytecode
	{
		const void* m_Data = nullptr;
		size_t m_SizeInBytes = 0;
		uint64_t m_HashLow = 0;
		uint64_t m_HashHigh = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Data != nullptr && m_SizeInBytes > 0;
		}
	};

	struct RHIGraphicsPipelineCreateInfo
	{
		RHIGraphicsPipelineDesc m_Desc{};
		RHIShaderBytecode m_VertexShader{};
		RHIShaderBytecode m_PixelShader{};
		RHIShaderBytecode m_DomainShader{};
		RHIShaderBytecode m_HullShader{};
		RHIShaderBytecode m_GeometryShader{};
	};

	struct RHIComputePipelineCreateInfo
	{
		RHIComputePipelineDesc m_Desc{};
		RHIShaderBytecode m_ComputeShader{};
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
