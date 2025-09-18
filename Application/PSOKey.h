#pragma once
#include "FNV1a.h"
#include "TypedIndex.h"

namespace gglab
{
	// RootSignatureId
	GGLAB_DEFINE_TYPED_INDEX(RootSignatureId, uint64_t);

	struct ShaderHash128
	{
		uint64_t m_LowBits = 0;
		uint64_t m_HighBits = 0;
	};

	struct PackedRasterizer
	{
		uint32_t m_Bits = 0;

		// Pack RasterizerDesc into 32 bits
		void PackRasterizerBits(const D3D12_RASTERIZER_DESC& desc) noexcept;
	};

	struct PackedDepth
	{
		uint32_t m_Bits = 0;

		// Pack DepthStencilDesc into 32 bits
		void PackDepthBits(const D3D12_DEPTH_STENCIL_DESC1& desc) noexcept;
	};

	struct PackedBlend
	{
		uint32_t m_Bits = 0;

		// Pack BlendDesc into 32 bits
		void PackBlendBits(const D3D12_BLEND_DESC& desc, uint32_t rtvCount) noexcept;
	};

	struct PSOKey
	{
		RootSignatureId m_RootSignatureId{};

		ShaderHash128 m_VertexShader{};
		ShaderHash128 m_PixelShader{};
		ShaderHash128 m_DomainShader{};
		ShaderHash128 m_HullShader{};
		ShaderHash128 m_GeometryShader{};

		uint32_t m_RtvCount = 0;
		DXGI_FORMAT m_Rtv[8]{};
		DXGI_FORMAT m_Dsv = DXGI_FORMAT_UNKNOWN;

		D3D12_PRIMITIVE_TOPOLOGY_TYPE m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		uint32_t m_SampleCount = 1;
		uint32_t m_SampleQuality = 0;
		uint32_t m_SampleMask = std::numeric_limits<uint32_t>::max();

		PackedRasterizer m_Rasterizer{};
		PackedDepth m_Depth{};
		PackedBlend m_Blend{};

		bool operator==(const PSOKey&) const noexcept = default;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_RootSignatureId.Value(),
				m_VertexShader.m_LowBits, m_VertexShader.m_HighBits,
				m_PixelShader.m_LowBits, m_PixelShader.m_HighBits,
				m_DomainShader.m_LowBits, m_DomainShader.m_HighBits,
				m_HullShader.m_LowBits, m_HullShader.m_HighBits,
				m_RtvCount,
				m_Rtv[0], m_Rtv[1], m_Rtv[2], m_Rtv[3], m_Rtv[4], m_Rtv[5], m_Rtv[6], m_Rtv[7],
				m_Dsv,
				m_Topology,
				m_SampleCount,
				m_SampleQuality,
				m_SampleMask,
				m_Rasterizer.m_Bits,
				m_Depth.m_Bits,
				m_Blend.m_Bits);
		}
	};
	using PSOKeyHash = KeyHash<PSOKey>;
}