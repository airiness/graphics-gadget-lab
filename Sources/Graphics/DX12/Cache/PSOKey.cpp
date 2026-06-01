#include "Core/Precompiled.h"
#include "Graphics/DX12/Cache/PSOKey.h"

namespace gglab
{
	void PackedRasterizer::PackRasterizerBits(const D3D12_RASTERIZER_DESC& desc) noexcept
	{
		// Pack D3D12_RASTERIZER_DESC into 32 bits
		uint32_t bits = 0;
		bits |= (static_cast<uint32_t>(desc.FillMode) & 0x3) << 0;				// 2 bits
		bits |= (static_cast<uint32_t>(desc.CullMode) & 0x3) << 2;				// 2 bits
		bits |= (desc.FrontCounterClockwise ? 1u : 0u) << 4;					// 1 bit
		bits |= (desc.DepthClipEnable ? 1u : 0u) << 5;							// 1 bit
		bits |= (static_cast<uint32_t>(desc.ConservativeRaster) & 0x3) << 6;	// 2 bits

		m_Bits = bits;
	}

	void PackedDepth::PackDepthBits(const D3D12_DEPTH_STENCIL_DESC1& desc) noexcept
	{
		uint32_t bits = 0;
		bits |= (desc.DepthEnable ? 1u : 0u) << 0;									// 1 bit
		bits |= (desc.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL ? 1u : 0u) << 1; // 1 bit
		bits |= (static_cast<uint32_t>(desc.DepthFunc) & 0x7u) << 2;				// 3 bits
		bits |= (desc.StencilEnable ? 1u : 0u) << 5;								// 1 bit

		m_Bits = bits;
	}

	void PackedBlend::PackBlendBits(const D3D12_BLEND_DESC& desc, uint32_t rtvCount) noexcept
	{
		uint32_t bits = 0;
		bits |= (desc.IndependentBlendEnable ? 1u : 0u) << 0;								// 1 bit
		if (rtvCount > 0)
		{
			bits |= (desc.RenderTarget[0].BlendEnable ? 1u : 0u) << 1;						// 1 bit
			bits |= (static_cast<uint32_t>(desc.RenderTarget[0].SrcBlend) & 0x1Fu) << 2;	// 5 bits
			bits |= (static_cast<uint32_t>(desc.RenderTarget[0].DestBlend) & 0x1Fu) << 7;	// 5 bits
			bits |= (static_cast<uint32_t>(desc.RenderTarget[0].BlendOp) & 0x7u) << 12;		// 3 bits
		}

		m_Bits = bits;
	}
}

