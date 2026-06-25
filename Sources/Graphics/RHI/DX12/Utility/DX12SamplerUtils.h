#pragma once
#include "Core/Platform/Win/ComTypes.h"
#include "Graphics/RHI/RHISampler.h"

namespace gglab
{
	[[nodiscard]] D3D12_FILTER ToD3D12Filter(RHISamplerFilter filter) noexcept;
	[[nodiscard]] D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(RHITextureAddressMode mode) noexcept;
	[[nodiscard]] D3D12_COMPARISON_FUNC ToD3D12ComparisonFunc(RHICompareOp compareOp) noexcept;
	[[nodiscard]] D3D12_SAMPLER_DESC ToD3D12SamplerDesc(const RHISamplerDesc& desc) noexcept;
}
