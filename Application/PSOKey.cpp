#include "Precompiled.h"
#include "PSOKey.h"

namespace gglab
{
	void PackedRasterizer::PackRasterizerBits(const D3D12_RASTERIZER_DESC& desc) noexcept
	{
		// Pack D3D12_RASTERIZER_DESC into 32 bits
		uint32_t bits = 0;
		bits |= (static_cast<uint32_t>(desc.FillMode) & 0x3) << 0;	// 2 bits
		bits |= (static_cast<uint32_t>(desc.CullMode) & 0x3) << 2;	// 2 bits
		bits |= (desc.FrontCounterClockwise ? 1u : 0u) << 4;	// 1 bit
		bits |= (desc.DepthBias & 0xFFFF) << 4;                                  // 16 bits
	}

	void PackedDepth::PackDepthBits(const D3D12_DEPTH_STENCIL_DESC& desc) noexcept
	{
	}

	void PackedBlend::PackBlendBits(const D3D12_BLEND_DESC& desc, uint32_t rtvCount) noexcept
	{
	}
}

