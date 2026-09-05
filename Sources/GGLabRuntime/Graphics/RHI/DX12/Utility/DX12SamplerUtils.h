#pragma once
#include "GGLabRuntime/Graphics/RHI/RHISampler.h"

#include <d3d12.h>

namespace gglab
{
	[[nodiscard]] D3D12_FILTER ToD3D12Filter(RHISamplerFilter filter) noexcept;
	[[nodiscard]] D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(
		RHITextureAddressMode mode) noexcept;
	[[nodiscard]] D3D12_SAMPLER_DESC ToD3D12SamplerDesc(const RHISamplerDesc& desc) noexcept;
}
