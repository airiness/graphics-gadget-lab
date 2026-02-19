#pragma once
#include "PSOKey.h"
namespace gglab
{
	// Graphics PSO desc
	struct GraphicsPipelineDesc
	{
		// Rootsignature
		RootSignatureID m_RootSignatureId{};
		ID3D12RootSignature* m_RootSignature = nullptr;

		// InputLayout
		InputLayoutID m_InputLayoutId{};
		D3D12_INPUT_LAYOUT_DESC m_InputLayoutDesc{};

		// Shaders
		D3D12_SHADER_BYTECODE m_VertexShader{};
		D3D12_SHADER_BYTECODE m_PixelShader{};
		D3D12_SHADER_BYTECODE m_DomainShader{};
		D3D12_SHADER_BYTECODE m_HullShader{};
		D3D12_SHADER_BYTECODE m_GeometryShader{};

		// Formats
		PipelineFormats m_Formats{};

		// Other states
		D3D12_PRIMITIVE_TOPOLOGY_TYPE m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		uint32_t m_SampleMask = std::numeric_limits<uint32_t>::max();

		CD3DX12_RASTERIZER_DESC m_RasterizerDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		CD3DX12_DEPTH_STENCIL_DESC1 m_DepthDesc = CD3DX12_DEPTH_STENCIL_DESC1(D3D12_DEFAULT);
		CD3DX12_BLEND_DESC m_BlendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

		// Generate PSO Key
		[[nodiscard]] GraphicsPSOKey MakeKey(ShaderHash128 vsHash, ShaderHash128 psHash,
			ShaderHash128 dsHash = {}, ShaderHash128 hsHash = {}, ShaderHash128 gsHash = {}) const noexcept;

		[[nodiscard]] bool Validate() const noexcept;
	};

	// Compute PSO desc
	struct ComputePipelineDesc
	{
		// Rootsignature
		RootSignatureID m_RootSignatureId{};
		ID3D12RootSignature* m_RootSignature = nullptr;
		// Shader
		D3D12_SHADER_BYTECODE m_ComputeShader{};

		[[nodiscard]] ComputePSOKey MakeKey(ShaderHash128 csHash) const noexcept;

		[[nodiscard]] bool Validate() const noexcept;
	};
}