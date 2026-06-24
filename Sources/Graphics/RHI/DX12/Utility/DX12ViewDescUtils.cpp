#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Utility/DX12ViewDescUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12FormatUtils.h"

namespace gglab
{
	namespace
	{
		DXGI_FORMAT ResolveViewFormat(RHIFormat format, const D3D12_RESOURCE_DESC& resourceDesc) noexcept
		{
			return format == RHIFormat::Unknown ? resourceDesc.Format : ToDXGIFormat(format);
		}

		UINT ResolveMipLevels(uint32_t mipLevels) noexcept
		{
			return mipLevels == RHITextureViewDesc::AllMipLevels ?
				static_cast<UINT>(-1) :
				static_cast<UINT>(mipLevels);
		}
	}

	D3D12_RENDER_TARGET_VIEW_DESC BuildD3D12RenderTargetViewDesc(
		const RHITextureViewDesc& desc,
		const D3D12_RESOURCE_DESC& resourceDesc) noexcept
	{
		D3D12_RENDER_TARGET_VIEW_DESC nativeDesc{};
		nativeDesc.Format = ResolveViewFormat(desc.m_Format, resourceDesc);

		const auto dimension = desc.m_Dimension == RHITextureViewDimension::Unknown ?
			(resourceDesc.DepthOrArraySize > 1 ? RHITextureViewDimension::Texture2DArray : RHITextureViewDimension::Texture2D) :
			desc.m_Dimension;

		switch (dimension)
		{
		case RHITextureViewDimension::Texture1D:
			nativeDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
			nativeDesc.Texture1D.MipSlice = desc.m_MipSlice;
			break;
		case RHITextureViewDimension::Texture1DArray:
			nativeDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
			nativeDesc.Texture1DArray.MipSlice = desc.m_MipSlice;
			nativeDesc.Texture1DArray.FirstArraySlice = desc.m_FirstArraySlice;
			nativeDesc.Texture1DArray.ArraySize = desc.m_ArraySize;
			break;
		case RHITextureViewDimension::Texture2D:
			nativeDesc.ViewDimension = resourceDesc.SampleDesc.Count > 1 ?
				D3D12_RTV_DIMENSION_TEXTURE2DMS :
				D3D12_RTV_DIMENSION_TEXTURE2D;
			if (nativeDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2D)
			{
				nativeDesc.Texture2D.MipSlice = desc.m_MipSlice;
				nativeDesc.Texture2D.PlaneSlice = desc.m_PlaneSlice;
			}
			break;
		case RHITextureViewDimension::Texture2DArray:
		case RHITextureViewDimension::TextureCube:
		case RHITextureViewDimension::TextureCubeArray:
			nativeDesc.ViewDimension = resourceDesc.SampleDesc.Count > 1 ?
				D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY :
				D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			if (nativeDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DARRAY)
			{
				nativeDesc.Texture2DArray.MipSlice = desc.m_MipSlice;
				nativeDesc.Texture2DArray.FirstArraySlice = desc.m_FirstArraySlice;
				nativeDesc.Texture2DArray.ArraySize = desc.m_ArraySize;
				nativeDesc.Texture2DArray.PlaneSlice = desc.m_PlaneSlice;
			}
			else
			{
				nativeDesc.Texture2DMSArray.FirstArraySlice = desc.m_FirstArraySlice;
				nativeDesc.Texture2DMSArray.ArraySize = desc.m_ArraySize;
			}
			break;
		case RHITextureViewDimension::Texture3D:
			nativeDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
			nativeDesc.Texture3D.MipSlice = desc.m_MipSlice;
			nativeDesc.Texture3D.FirstWSlice = desc.m_FirstArraySlice;
			nativeDesc.Texture3D.WSize = desc.m_ArraySize;
			break;
		case RHITextureViewDimension::Unknown:
			GGLAB_UNREACHABLE("Unexpected unknown RHI texture view dimension.");
		}

		return nativeDesc;
	}

	D3D12_DEPTH_STENCIL_VIEW_DESC BuildD3D12DepthStencilViewDesc(
		const RHITextureViewDesc& desc,
		const D3D12_RESOURCE_DESC& resourceDesc) noexcept
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC nativeDesc{};
		nativeDesc.Format = ResolveViewFormat(desc.m_Format, resourceDesc);

		const auto dimension = desc.m_Dimension == RHITextureViewDimension::Unknown ?
			(resourceDesc.DepthOrArraySize > 1 ? RHITextureViewDimension::Texture2DArray : RHITextureViewDimension::Texture2D) :
			desc.m_Dimension;

		if (dimension == RHITextureViewDimension::Texture2DArray)
		{
			nativeDesc.ViewDimension = resourceDesc.SampleDesc.Count > 1 ?
				D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY :
				D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			if (nativeDesc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DARRAY)
			{
				nativeDesc.Texture2DArray.MipSlice = desc.m_MipSlice;
				nativeDesc.Texture2DArray.FirstArraySlice = desc.m_FirstArraySlice;
				nativeDesc.Texture2DArray.ArraySize = desc.m_ArraySize;
			}
			else
			{
				nativeDesc.Texture2DMSArray.FirstArraySlice = desc.m_FirstArraySlice;
				nativeDesc.Texture2DMSArray.ArraySize = desc.m_ArraySize;
			}
		}
		else
		{
			nativeDesc.ViewDimension = resourceDesc.SampleDesc.Count > 1 ?
				D3D12_DSV_DIMENSION_TEXTURE2DMS :
				D3D12_DSV_DIMENSION_TEXTURE2D;
			if (nativeDesc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2D)
			{
				nativeDesc.Texture2D.MipSlice = desc.m_MipSlice;
			}
		}

		return nativeDesc;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC BuildD3D12ShaderResourceViewDesc(
		const RHITextureViewDesc& desc,
		const D3D12_RESOURCE_DESC& resourceDesc) noexcept
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC nativeDesc{};
		nativeDesc.Format = ResolveViewFormat(desc.m_Format, resourceDesc);
		nativeDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		const auto dimension = desc.m_Dimension == RHITextureViewDimension::Unknown ?
			(resourceDesc.DepthOrArraySize > 1 ? RHITextureViewDimension::Texture2DArray : RHITextureViewDimension::Texture2D) :
			desc.m_Dimension;

		switch (dimension)
		{
		case RHITextureViewDimension::Texture1D:
			nativeDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
			nativeDesc.Texture1D.MostDetailedMip = desc.m_MostDetailedMip;
			nativeDesc.Texture1D.MipLevels = ResolveMipLevels(desc.m_MipLevels);
			nativeDesc.Texture1D.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			break;
		case RHITextureViewDimension::Texture1DArray:
			nativeDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
			nativeDesc.Texture1DArray.MostDetailedMip = desc.m_MostDetailedMip;
			nativeDesc.Texture1DArray.MipLevels = ResolveMipLevels(desc.m_MipLevels);
			nativeDesc.Texture1DArray.FirstArraySlice = desc.m_FirstArraySlice;
			nativeDesc.Texture1DArray.ArraySize = desc.m_ArraySize;
			nativeDesc.Texture1DArray.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			break;
		case RHITextureViewDimension::Texture2D:
			nativeDesc.ViewDimension = resourceDesc.SampleDesc.Count > 1 ?
				D3D12_SRV_DIMENSION_TEXTURE2DMS :
				D3D12_SRV_DIMENSION_TEXTURE2D;
			if (nativeDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D)
			{
				nativeDesc.Texture2D.MostDetailedMip = desc.m_MostDetailedMip;
				nativeDesc.Texture2D.MipLevels = ResolveMipLevels(desc.m_MipLevels);
				nativeDesc.Texture2D.PlaneSlice = desc.m_PlaneSlice;
				nativeDesc.Texture2D.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			}
			break;
		case RHITextureViewDimension::Texture2DArray:
			nativeDesc.ViewDimension = resourceDesc.SampleDesc.Count > 1 ?
				D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY :
				D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			if (nativeDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY)
			{
				nativeDesc.Texture2DArray.MostDetailedMip = desc.m_MostDetailedMip;
				nativeDesc.Texture2DArray.MipLevels = ResolveMipLevels(desc.m_MipLevels);
				nativeDesc.Texture2DArray.FirstArraySlice = desc.m_FirstArraySlice;
				nativeDesc.Texture2DArray.ArraySize = desc.m_ArraySize;
				nativeDesc.Texture2DArray.PlaneSlice = desc.m_PlaneSlice;
				nativeDesc.Texture2DArray.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			}
			else
			{
				nativeDesc.Texture2DMSArray.FirstArraySlice = desc.m_FirstArraySlice;
				nativeDesc.Texture2DMSArray.ArraySize = desc.m_ArraySize;
			}
			break;
		case RHITextureViewDimension::Texture3D:
			nativeDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			nativeDesc.Texture3D.MostDetailedMip = desc.m_MostDetailedMip;
			nativeDesc.Texture3D.MipLevels = ResolveMipLevels(desc.m_MipLevels);
			nativeDesc.Texture3D.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			break;
		case RHITextureViewDimension::TextureCube:
			nativeDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			nativeDesc.TextureCube.MostDetailedMip = desc.m_MostDetailedMip;
			nativeDesc.TextureCube.MipLevels = ResolveMipLevels(desc.m_MipLevels);
			nativeDesc.TextureCube.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			break;
		case RHITextureViewDimension::TextureCubeArray:
			nativeDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
			nativeDesc.TextureCubeArray.MostDetailedMip = desc.m_MostDetailedMip;
			nativeDesc.TextureCubeArray.MipLevels = ResolveMipLevels(desc.m_MipLevels);
			nativeDesc.TextureCubeArray.First2DArrayFace = desc.m_First2DArrayFace;
			nativeDesc.TextureCubeArray.NumCubes = desc.m_NumCubes;
			nativeDesc.TextureCubeArray.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			break;
		case RHITextureViewDimension::Unknown:
			GGLAB_UNREACHABLE("Unexpected unknown RHI texture view dimension.");
		}

		return nativeDesc;
	}

	D3D12_UNORDERED_ACCESS_VIEW_DESC BuildD3D12UnorderedAccessViewDesc(
		const RHITextureViewDesc& desc,
		const D3D12_RESOURCE_DESC& resourceDesc) noexcept
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC nativeDesc{};
		nativeDesc.Format = ResolveViewFormat(desc.m_Format, resourceDesc);

		const auto dimension = desc.m_Dimension == RHITextureViewDimension::Unknown ?
			RHITextureViewDimension::Texture2D :
			desc.m_Dimension;

		switch (dimension)
		{
		case RHITextureViewDimension::Texture1D:
			nativeDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
			nativeDesc.Texture1D.MipSlice = desc.m_MipSlice;
			break;
		case RHITextureViewDimension::Texture1DArray:
			nativeDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
			nativeDesc.Texture1DArray.MipSlice = desc.m_MipSlice;
			nativeDesc.Texture1DArray.FirstArraySlice = desc.m_FirstArraySlice;
			nativeDesc.Texture1DArray.ArraySize = desc.m_ArraySize;
			break;
		case RHITextureViewDimension::Texture2D:
			nativeDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			nativeDesc.Texture2D.MipSlice = desc.m_MipSlice;
			nativeDesc.Texture2D.PlaneSlice = desc.m_PlaneSlice;
			break;
		case RHITextureViewDimension::Texture2DArray:
		case RHITextureViewDimension::TextureCube:
		case RHITextureViewDimension::TextureCubeArray:
			nativeDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			nativeDesc.Texture2DArray.MipSlice = desc.m_MipSlice;
			nativeDesc.Texture2DArray.FirstArraySlice = desc.m_FirstArraySlice;
			nativeDesc.Texture2DArray.ArraySize = desc.m_ArraySize;
			nativeDesc.Texture2DArray.PlaneSlice = desc.m_PlaneSlice;
			break;
		case RHITextureViewDimension::Texture3D:
			nativeDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
			nativeDesc.Texture3D.MipSlice = desc.m_MipSlice;
			nativeDesc.Texture3D.FirstWSlice = desc.m_FirstArraySlice;
			nativeDesc.Texture3D.WSize = desc.m_ArraySize;
			break;
		case RHITextureViewDimension::Unknown:
			GGLAB_UNREACHABLE("Unexpected unknown RHI texture view dimension.");
		}

		return nativeDesc;
	}
}
