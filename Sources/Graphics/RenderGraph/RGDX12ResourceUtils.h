#pragma once
#include "Graphics/RenderGraph/RGResourceUtils.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/Utility/DX12FormatUtils.h"

namespace gglab
{
	inline RGTextureDesc ToRGTextureDesc(const D3D12_RESOURCE_DESC& nativeDesc, RGTextureUsage usage) noexcept
	{
		RGTextureDesc rgDesc{};
		rgDesc.m_Width = static_cast<uint32_t>(nativeDesc.Width);
		rgDesc.m_Height = static_cast<uint32_t>(nativeDesc.Height);
		rgDesc.m_ArraySize = static_cast<uint16_t>(nativeDesc.DepthOrArraySize);
		rgDesc.m_MipLevels = static_cast<uint16_t>(nativeDesc.MipLevels);
		rgDesc.m_SampleCount = static_cast<uint16_t>(nativeDesc.SampleDesc.Count);
		rgDesc.m_Format = ToRHIFormat(nativeDesc.Format);
		rgDesc.m_Usage = usage;
		return rgDesc;
	}

	inline RGTextureDesc ToRGTextureDesc(const DX12Texture& texture, RGTextureUsage usage) noexcept
	{
		return ToRGTextureDesc(texture.GetDesc(), usage);
	}

	inline D3D12_RENDER_TARGET_VIEW_DESC ToD3D12RenderTargetViewDesc(const RHITextureViewDesc& rgDesc) noexcept
	{
		D3D12_RENDER_TARGET_VIEW_DESC desc{};
		desc.Format = ToDXGIFormat(rgDesc.m_Format);

		switch (rgDesc.m_Dimension)
		{
		case RHITextureViewDimension::Texture2DArray:
			desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.MipSlice = rgDesc.m_MipSlice;
			desc.Texture2DArray.FirstArraySlice = rgDesc.m_FirstArraySlice;
			desc.Texture2DArray.ArraySize = rgDesc.m_ArraySize;
			desc.Texture2DArray.PlaneSlice = rgDesc.m_PlaneSlice;
			break;
		case RHITextureViewDimension::Unknown:
		case RHITextureViewDimension::Texture2D:
			desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = rgDesc.m_MipSlice;
			desc.Texture2D.PlaneSlice = rgDesc.m_PlaneSlice;
			break;
		default:
			GGLAB_UNREACHABLE("Unsupported RHITextureViewDimension for RTV.");
		}

		return desc;
	}

	inline D3D12_DEPTH_STENCIL_VIEW_DESC ToD3D12DepthStencilViewDesc(const RHITextureViewDesc& rgDesc) noexcept
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC desc{};
		desc.Format = ToDXGIFormat(rgDesc.m_Format);

		switch (rgDesc.m_Dimension)
		{
		case RHITextureViewDimension::Unknown:
		case RHITextureViewDimension::Texture2D:
			desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			desc.Flags = D3D12_DSV_FLAG_NONE;
			desc.Texture2D.MipSlice = rgDesc.m_MipSlice;
			break;
		default:
			GGLAB_UNREACHABLE("Unsupported RHITextureViewDimension for DSV.");
		}

		return desc;
	}

	inline D3D12_SHADER_RESOURCE_VIEW_DESC ToD3D12ShaderResourceViewDesc(const RHITextureViewDesc& rgDesc) noexcept
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
		desc.Format = ToDXGIFormat(rgDesc.m_Format);
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		const auto mipLevels =
			rgDesc.m_MipLevels == RHITextureViewDesc::AllMipLevels ?
			static_cast<UINT>(-1) :
			rgDesc.m_MipLevels;

		switch (rgDesc.m_Dimension)
		{
		case RHITextureViewDimension::Unknown:
		case RHITextureViewDimension::Texture2D:
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MostDetailedMip = rgDesc.m_MostDetailedMip;
			desc.Texture2D.MipLevels = mipLevels;
			desc.Texture2D.PlaneSlice = rgDesc.m_PlaneSlice;
			desc.Texture2D.ResourceMinLODClamp = rgDesc.m_ResourceMinLODClamp;
			break;
		case RHITextureViewDimension::Texture2DArray:
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.MostDetailedMip = rgDesc.m_MostDetailedMip;
			desc.Texture2DArray.MipLevels = mipLevels;
			desc.Texture2DArray.FirstArraySlice = rgDesc.m_FirstArraySlice;
			desc.Texture2DArray.ArraySize = rgDesc.m_ArraySize;
			desc.Texture2DArray.PlaneSlice = rgDesc.m_PlaneSlice;
			desc.Texture2DArray.ResourceMinLODClamp = rgDesc.m_ResourceMinLODClamp;
			break;
		case RHITextureViewDimension::TextureCube:
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			desc.TextureCube.MostDetailedMip = rgDesc.m_MostDetailedMip;
			desc.TextureCube.MipLevels = mipLevels;
			desc.TextureCube.ResourceMinLODClamp = rgDesc.m_ResourceMinLODClamp;
			break;
		case RHITextureViewDimension::TextureCubeArray:
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
			desc.TextureCubeArray.MostDetailedMip = rgDesc.m_MostDetailedMip;
			desc.TextureCubeArray.MipLevels = mipLevels;
			desc.TextureCubeArray.First2DArrayFace = rgDesc.m_First2DArrayFace;
			desc.TextureCubeArray.NumCubes = rgDesc.m_NumCubes;
			desc.TextureCubeArray.ResourceMinLODClamp = rgDesc.m_ResourceMinLODClamp;
			break;
		}

		return desc;
	}

	inline D3D12_UNORDERED_ACCESS_VIEW_DESC ToD3D12UnorderedAccessViewDesc(const RHITextureViewDesc& rgDesc) noexcept
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
		desc.Format = ToDXGIFormat(rgDesc.m_Format);

		switch (rgDesc.m_Dimension)
		{
		case RHITextureViewDimension::Unknown:
		case RHITextureViewDimension::Texture2D:
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = rgDesc.m_MipSlice;
			desc.Texture2D.PlaneSlice = rgDesc.m_PlaneSlice;
			break;
		case RHITextureViewDimension::Texture2DArray:
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.MipSlice = rgDesc.m_MipSlice;
			desc.Texture2DArray.FirstArraySlice = rgDesc.m_FirstArraySlice;
			desc.Texture2DArray.ArraySize = rgDesc.m_ArraySize;
			desc.Texture2DArray.PlaneSlice = rgDesc.m_PlaneSlice;
			break;
		default:
			GGLAB_UNREACHABLE("Unsupported RHITextureViewDimension for UAV.");
		}

		return desc;
	}
}
