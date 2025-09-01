#include "Precompiled.h"
#include "ViewCache.h"
#include "DX12Texture.h"

namespace gglab
{
	ViewCache::BuiltRTV ViewCache::CreateRTVKey(uint32_t resourceIndex, 
		DX12Texture* texture, 
		const D3D12_RENDER_TARGET_VIEW_DESC& inDesc) noexcept
	{
		BuiltRTV built{};
		auto& outDesc = built.m_Desc;

		const auto& texDesc = texture->GetDesc();
		outDesc = inDesc;

		// TODO: typeless resource? format mapping?
		if (outDesc.Format == DXGI_FORMAT_UNKNOWN)
		{
			outDesc.Format = texDesc.Format;
		}

		if (outDesc.ViewDimension == D3D12_RTV_DIMENSION_UNKNOWN)
		{
			outDesc.ViewDimension = (texDesc.SampleDesc.Count > 1) ?
				D3D12_RTV_DIMENSION_TEXTURE2DMS :
				D3D12_RTV_DIMENSION_TEXTURE2D;
		}

		uint16_t mipSlice = 0;
		uint8_t planeSlice = 0;

		switch (outDesc.ViewDimension)
		{
		case D3D12_RTV_DIMENSION_TEXTURE2D:
			mipSlice = static_cast<uint16_t>(outDesc.Texture2D.MipSlice);
			planeSlice = static_cast<uint8_t>(outDesc.Texture2D.PlaneSlice);
			break;
		case D3D12_RTV_DIMENSION_TEXTURE2DMS:
			break;
		default:
			break;
		}

		auto& key = built.m_ViewKey;
		key.m_Type = Type::RTV;
		key.m_ResouceIndex = resourceIndex;
		key.m_Format = outDesc.Format;
		key.m_MipSlice = mipSlice;
		key.m_ArraySlice = 0;
		key.m_PlaneSlice = planeSlice;
		key.m_Dimension = static_cast<uint8_t>(outDesc.ViewDimension);
		key.m_Flags = 0;

		return built;	
	}

	ViewCache::BuiltDSV ViewCache::CreateDSVKey(uint32_t resourceIndex, 
		DX12Texture* texture, 
		const D3D12_DEPTH_STENCIL_VIEW_DESC& inDesc) noexcept
	{
		BuiltDSV built{};
		auto& outDesc = built.m_Desc;

		const auto& texDesc = texture->GetDesc();
		outDesc = inDesc;

		if (outDesc.Format == DXGI_FORMAT_UNKNOWN)
		{
			outDesc.Format = texDesc.Format;
		}

		if (outDesc.ViewDimension == D3D12_DSV_DIMENSION_UNKNOWN)
		{
			outDesc.ViewDimension = (texDesc.SampleDesc.Count > 1) ?
				D3D12_DSV_DIMENSION_TEXTURE2DMS :
				D3D12_DSV_DIMENSION_TEXTURE2D;
		}

		uint16_t mipSlice = 0;
		switch (outDesc.ViewDimension)
		{
		case D3D12_DSV_DIMENSION_TEXTURE2D:
			mipSlice = static_cast<uint16_t>(outDesc.Texture2D.MipSlice);
			break;
		case D3D12_DSV_DIMENSION_TEXTURE2DMS:
			break;
		default:
			break;
		}


	}

	ViewCache::ViewCache(DX12Device* dx12Device, const DescriptorsAllocatorArray& descriptorAllocators) noexcept:
		m_DX12Device(dx12Device),
		m_DescriptorAllocators(descriptorAllocators)
	{
	}

	ViewCache::~ViewCache()
	{

	}

}