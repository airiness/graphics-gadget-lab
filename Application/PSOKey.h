#pragma once
#include "FNV1a.h"
#include "TypedIndex.h"
#include "GraphicsTypes.h"
#include "PipelinePresets.h"

namespace gglab
{
	struct ShaderHash128
	{
		uint64_t m_LowBits = 0;
		uint64_t m_HighBits = 0;
		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_LowBits, m_HighBits);
		}
		constexpr bool operator==(const ShaderHash128& rhs) const noexcept = default;
	};

	struct PipelineFormats
	{
		D3D12_RT_FORMAT_ARRAY m_RtvFormats = {};
		DXGI_FORMAT m_DsvFormat = DXGI_FORMAT_UNKNOWN;
		uint32_t m_SampleCount = 1;
		uint32_t m_SampleQuality = 0;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_RtvFormats.NumRenderTargets,
				m_RtvFormats.RTFormats[0],
				m_RtvFormats.RTFormats[1],
				m_RtvFormats.RTFormats[2],
				m_RtvFormats.RTFormats[3],
				m_RtvFormats.RTFormats[4],
				m_RtvFormats.RTFormats[5],
				m_RtvFormats.RTFormats[6],
				m_RtvFormats.RTFormats[7],
				m_DsvFormat,
				m_SampleCount,
				m_SampleQuality);
		}
		constexpr bool operator==(const PipelineFormats& rhs) const noexcept
		{
			return AsTuple() == rhs.AsTuple();
		}
	};

	struct PackedRasterizer
	{
		uint32_t m_Bits = 0;

		constexpr bool operator==(const PackedRasterizer&) const noexcept = default;

		// Pack RasterizerDesc into 32 bits
		void PackRasterizerBits(const D3D12_RASTERIZER_DESC& desc) noexcept;
	};

	struct PackedDepth
	{
		uint32_t m_Bits = 0;
	
		constexpr bool operator==(const PackedDepth&) const noexcept = default;

		// Pack DepthStencilDesc into 32 bits
		void PackDepthBits(const D3D12_DEPTH_STENCIL_DESC1& desc) noexcept;
	};

	struct PackedBlend
	{
		uint32_t m_Bits = 0;

		constexpr bool operator==(const PackedBlend&) const noexcept = default;

		// Pack BlendDesc into 32 bits
		void PackBlendBits(const D3D12_BLEND_DESC& desc, uint32_t rtvCount) noexcept;
	};

	// GraphicsPSOKey
	struct GraphicsPSOKey
	{
		RootSignatureId m_RootSignatureId{};
		InputLayoutId m_InputLayoutId{};

		ShaderHash128 m_VertexShader{};
		ShaderHash128 m_PixelShader{};
		ShaderHash128 m_DomainShader{};
		ShaderHash128 m_HullShader{};
		ShaderHash128 m_GeometryShader{};

		PipelineFormats m_Formats{};

		D3D12_PRIMITIVE_TOPOLOGY_TYPE m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		uint32_t m_SampleMask = std::numeric_limits<uint32_t>::max();

		PackedRasterizer m_Rasterizer{};
		PackedDepth m_Depth{};
		PackedBlend m_Blend{};

		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_RootSignatureId.Value(),
				m_InputLayoutId,
				m_VertexShader.m_LowBits, m_VertexShader.m_HighBits,
				m_PixelShader.m_LowBits, m_PixelShader.m_HighBits,
				m_DomainShader.m_LowBits, m_DomainShader.m_HighBits,
				m_HullShader.m_LowBits, m_HullShader.m_HighBits,
				m_GeometryShader.m_LowBits, m_GeometryShader.m_HighBits,
				m_Formats.AsTuple(),
				m_Topology,
				m_SampleMask,
				m_Rasterizer.m_Bits,
				m_Depth.m_Bits,
				m_Blend.m_Bits);
		}
		constexpr bool operator==(const GraphicsPSOKey&) const noexcept = default;
	};
	using GraphicsPSOKeyHash = KeyHash<GraphicsPSOKey>;

	// ComputePSOKey
	struct ComputePSOKey
	{
		RootSignatureId m_RootSignatureId{};
		ShaderHash128 m_ComputeShader{};

		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_RootSignatureId.Value(),
				m_ComputeShader.m_LowBits, m_ComputeShader.m_HighBits);
		}
		constexpr bool operator==(const ComputePSOKey&) const noexcept = default;
	};
	using ComputePSOKeyHash = KeyHash<ComputePSOKey>;
}