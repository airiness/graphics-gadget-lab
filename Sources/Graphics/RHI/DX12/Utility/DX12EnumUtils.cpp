#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Utility/DX12EnumUtils.h"

namespace gglab
{
	D3D12_COMPARISON_FUNC ToD3D12ComparisonFunc(RHICompareOp compareOp) noexcept
	{
		switch (compareOp)
		{
		case RHICompareOp::Never:
			return D3D12_COMPARISON_FUNC_NEVER;
		case RHICompareOp::Less:
			return D3D12_COMPARISON_FUNC_LESS;
		case RHICompareOp::Equal:
			return D3D12_COMPARISON_FUNC_EQUAL;
		case RHICompareOp::LessEqual:
			return D3D12_COMPARISON_FUNC_LESS_EQUAL;
		case RHICompareOp::Greater:
			return D3D12_COMPARISON_FUNC_GREATER;
		case RHICompareOp::NotEqual:
			return D3D12_COMPARISON_FUNC_NOT_EQUAL;
		case RHICompareOp::GreaterEqual:
			return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		case RHICompareOp::Always:
		default:
			return D3D12_COMPARISON_FUNC_ALWAYS;
		}
	}
}
