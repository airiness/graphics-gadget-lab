#pragma once
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <d3d12.h>

namespace gglab
{
	[[nodiscard]] D3D12_COMPARISON_FUNC ToD3D12ComparisonFunc(RHICompareOp compareOp) noexcept;
}
