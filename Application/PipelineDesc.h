#pragma once
#include "PSOKey.h"
namespace gglab
{
	// Descript Graphics PSO
	struct GraphicsPipelineDesc
	{
		// Rootsignature
		RootSignatureId m_RootSignatureId{};
		ID3D12RootSignature* m_RootSignature = nullptr;

		// InputLayout
		std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputLayout;

		// Shaders
		D3D12_SHADER_BYTECODE m_VertexShader{};
		D3D12_SHADER_BYTECODE m_PixelShader{};
		D3D12_SHADER_BYTECODE m_DomainShader{};
		D3D12_SHADER_BYTECODE m_HullShader{};
		D3D12_SHADER_BYTECODE m_GeometryShader{};

		// Formats
		std::array<DXGI_FORMAT, 8> m_RtvFormats{};
		uint32_t m_RtvCount = 0;
		DXGI_FORMAT m_DsvFormat = DXGI_FORMAT_UNKNOWN;

		// Other states
		D3D12_PRIMITIVE_TOPOLOGY_TYPE m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		uint32_t m_SampleCount = 1;
		uint32_t m_SampleQuality = 0;
		uint32_t m_SampleMask = std::numeric_limits<uint32_t>::max();

		D3D12_RASTERIZER_DESC m_RasterizerDesc{};
		D3D12_DEPTH_STENCIL_DESC1 m_DepthDesc{};
		D3D12_BLEND_DESC m_BlendDesc{};

		// Build Stream
		D3D12_PIPELINE_STATE_STREAM_DESC ToStreamDesc(void** outStreamStorage, size_t outStreamCount) const noexcept;

		// Generate PSO Key
		PSOKey MakeKey(ShaderHash128 vsHash, ShaderHash128 psHash,
			ShaderHash128 dsHash = {}, ShaderHash128 hsHash = {}, ShaderHash128 gsHash = {}) const noexcept;

		bool Validate() const noexcept;
	};
}