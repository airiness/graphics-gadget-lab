#pragma once

#include <d3d12.h>

namespace gglab
{
	[[nodiscard]] inline D3D12_RENDER_TARGET_VIEW_DESC MakeTexture2DArrayRtvDesc(DXGI_FORMAT format,
		UINT mip,
		UINT firstSlice,
		UINT arraySize,
		UINT planeSlice = 0) noexcept
	{
		D3D12_RENDER_TARGET_VIEW_DESC desc{};
		desc.Format = format;
		desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
		desc.Texture2DArray.MipSlice = mip;
		desc.Texture2DArray.FirstArraySlice = firstSlice;
		desc.Texture2DArray.ArraySize = arraySize;
		desc.Texture2DArray.PlaneSlice = planeSlice;
		return desc;
	}

	[[nodiscard]] inline D3D12_SHADER_RESOURCE_VIEW_DESC MakeTextureCubeSrvDesc(DXGI_FORMAT format,
		UINT mostMip,
		UINT mipLevels,
		float resourceMinLODClamp = 0.0f,
		UINT shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING) noexcept
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
		desc.Format = format;
		desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		desc.Shader4ComponentMapping = shader4ComponentMapping;
		desc.TextureCube.MostDetailedMip = mostMip;
		desc.TextureCube.MipLevels = mipLevels;
		desc.TextureCube.ResourceMinLODClamp = resourceMinLODClamp;
		return desc;
	}

	[[nodiscard]] inline D3D12_SHADER_RESOURCE_VIEW_DESC MakeTextureCubeArraySrvDesc(DXGI_FORMAT format,
		UINT firstFace,
		UINT numCubes,
		UINT mostMip,
		UINT mipLevels,
		float resourceMinLODClamp = 0.0f,
		UINT shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING) noexcept
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
		desc.Format = format;
		desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
		desc.Shader4ComponentMapping = shader4ComponentMapping;
		desc.TextureCubeArray.MostDetailedMip = mostMip;
		desc.TextureCubeArray.MipLevels = mipLevels;
		desc.TextureCubeArray.First2DArrayFace = firstFace;
		desc.TextureCubeArray.NumCubes = numCubes;
		desc.TextureCubeArray.ResourceMinLODClamp = resourceMinLODClamp;
		return desc;
	}
}
