#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Utility/DX12SamplerUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12EnumUtils.h"

namespace gglab
{
	D3D12_FILTER ToD3D12Filter(RHISamplerFilter filter) noexcept
	{
		switch (filter)
		{
		case RHISamplerFilter::MinMagMipPoint:
			return D3D12_FILTER_MIN_MAG_MIP_POINT;
		case RHISamplerFilter::MinMagPointMipLinear:
			return D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
		case RHISamplerFilter::MinPointMagLinearMipPoint:
			return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
		case RHISamplerFilter::MinPointMagMipLinear:
			return D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
		case RHISamplerFilter::MinLinearMagMipPoint:
			return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
		case RHISamplerFilter::MinLinearMagPointMipLinear:
			return D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
		case RHISamplerFilter::MinMagLinearMipPoint:
			return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		case RHISamplerFilter::MinMagMipLinear:
			return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		case RHISamplerFilter::Anisotropic:
			return D3D12_FILTER_ANISOTROPIC;
		case RHISamplerFilter::ComparisonMinMagLinearMipPoint:
			return D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		case RHISamplerFilter::ComparisonAnisotropic:
			return D3D12_FILTER_COMPARISON_ANISOTROPIC;
		default:
			return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		}
	}

	D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(RHITextureAddressMode mode) noexcept
	{
		switch (mode)
		{
		case RHITextureAddressMode::Wrap:
			return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		case RHITextureAddressMode::Mirror:
			return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		case RHITextureAddressMode::Clamp:
			return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		case RHITextureAddressMode::Border:
			return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		case RHITextureAddressMode::MirrorOnce:
			return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
		default:
			return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		}
	}

	D3D12_SAMPLER_DESC ToD3D12SamplerDesc(const RHISamplerDesc& key) noexcept
	{
		D3D12_SAMPLER_DESC desc{};

		desc.Filter = ToD3D12Filter(key.m_Filter);
		desc.AddressU = ToD3D12AddressMode(key.m_AddressU);
		desc.AddressV = ToD3D12AddressMode(key.m_AddressV);
		desc.AddressW = ToD3D12AddressMode(key.m_AddressW);
		desc.MipLODBias = key.m_MipLODBias;
		desc.MaxAnisotropy = key.m_MaxAnisotropy;
		desc.ComparisonFunc = ToD3D12ComparisonFunc(key.m_CompareOp);
		desc.BorderColor[0] = key.m_BorderColor[0];
		desc.BorderColor[1] = key.m_BorderColor[1];
		desc.BorderColor[2] = key.m_BorderColor[2];
		desc.BorderColor[3] = key.m_BorderColor[3];
		desc.MinLOD = key.m_MinLOD;
		desc.MaxLOD = key.m_MaxLOD;

		return desc;
	}
}
