#pragma once
#include "Graphics/RHI/DX12/Cache/PipelineDesc.h"
#include "Graphics/RHI/DX12/Cache/DX12RootSignatureCache.h"
#include "Graphics/RHI/DX12/Utility/DX12InputLayoutUtils.h"
#include "Graphics/RHI/RHIPipeline.h"

namespace gglab
{
	struct DX12GraphicsPipelineShaderInputs
	{
		D3D12_SHADER_BYTECODE m_VertexShader{};
		D3D12_SHADER_BYTECODE m_PixelShader{};
		D3D12_SHADER_BYTECODE m_DomainShader{};
		D3D12_SHADER_BYTECODE m_HullShader{};
		D3D12_SHADER_BYTECODE m_GeometryShader{};

		ShaderHash128 m_VertexShaderHash{};
		ShaderHash128 m_PixelShaderHash{};
		ShaderHash128 m_DomainShaderHash{};
		ShaderHash128 m_HullShaderHash{};
		ShaderHash128 m_GeometryShaderHash{};
	};

	struct DX12RHIGraphicsPSOKey
	{
		uint64_t m_LowBits = 0;
		uint64_t m_HighBits = 0;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_LowBits, m_HighBits);
		}
		constexpr bool operator==(const DX12RHIGraphicsPSOKey&) const noexcept = default;
	};
	using DX12RHIGraphicsPSOKeyHash = KeyHash<DX12RHIGraphicsPSOKey>;

	[[nodiscard]] GraphicsPipelineDesc BuildDX12GraphicsPipelineDesc(
		const RHIGraphicsPipelineDesc& rhiDesc,
		const RootSignatureHandle& rootSignature,
		const DX12GraphicsPipelineShaderInputs& shaderInputs,
		const DX12InputLayoutBuildResult& inputLayout) noexcept;

	[[nodiscard]] DX12RHIGraphicsPSOKey MakeDX12RHIGraphicsPSOKey(
		const RHIGraphicsPipelineDesc& rhiDesc,
		RootSignatureID rootSignatureId,
		const DX12GraphicsPipelineShaderInputs& shaderInputs) noexcept;
}
