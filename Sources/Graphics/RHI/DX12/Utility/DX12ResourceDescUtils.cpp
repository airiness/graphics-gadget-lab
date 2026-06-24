#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Utility/DX12ResourceDescUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12FormatUtils.h"

namespace gglab
{
	D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(RHITextureUsage usage) noexcept
	{
		return
			(Test(usage, RHITextureUsage::RenderTarget) ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE) |
			(Test(usage, RHITextureUsage::DepthStencil) ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_NONE) |
			(Test(usage, RHITextureUsage::UnorderedAccess) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
	}

	D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(RHIBufferUsage usage) noexcept
	{
		return Test(usage, RHIBufferUsage::UnorderedAccess) ?
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS :
			D3D12_RESOURCE_FLAG_NONE;
	}

	std::optional<D3D12_CLEAR_VALUE> ToD3D12ClearValue(const std::optional<RHIClearValue>& clearValue) noexcept
	{
		if (!clearValue.has_value())
		{
			return std::nullopt;
		}

		return ToD3D12ClearValue(clearValue.value());
	}

	D3D12_CLEAR_VALUE ToD3D12ClearValue(const RHIClearValue& clearValue) noexcept
	{
		D3D12_CLEAR_VALUE nativeClearValue{};
		nativeClearValue.Format = ToDXGIFormat(clearValue.m_Format);

		if (clearValue.m_IsDepthStencil)
		{
			nativeClearValue.DepthStencil.Depth = clearValue.m_Depth;
			nativeClearValue.DepthStencil.Stencil = clearValue.m_Stencil;
		}
		else
		{
			nativeClearValue.Color[0] = clearValue.m_Color[0];
			nativeClearValue.Color[1] = clearValue.m_Color[1];
			nativeClearValue.Color[2] = clearValue.m_Color[2];
			nativeClearValue.Color[3] = clearValue.m_Color[3];
		}

		return nativeClearValue;
	}

	CD3DX12_RESOURCE_DESC ToD3D12ResourceDesc(const RHITextureDesc& desc) noexcept
	{
		D3D12_RESOURCE_DESC nativeDesc{};
		nativeDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		nativeDesc.Alignment = 0;
		nativeDesc.Width = static_cast<UINT64>(desc.m_Extent.m_Width);
		nativeDesc.Height = desc.m_Extent.m_Height;
		nativeDesc.DepthOrArraySize = desc.m_ArraySize;
		nativeDesc.MipLevels = desc.m_MipLevels;
		nativeDesc.Format = ToDXGIFormat(desc.m_Format);
		nativeDesc.SampleDesc.Count = desc.m_SampleCount;
		nativeDesc.SampleDesc.Quality = 0;
		nativeDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		nativeDesc.Flags = ToD3D12ResourceFlags(desc.m_Usage);

		switch (desc.m_Dimension)
		{
		case RHITextureDimension::Texture1D:
			nativeDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
			nativeDesc.Height = 1;
			nativeDesc.DepthOrArraySize = desc.m_ArraySize;
			break;
		case RHITextureDimension::Texture2D:
			nativeDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			nativeDesc.DepthOrArraySize = desc.m_ArraySize;
			break;
		case RHITextureDimension::Texture3D:
			nativeDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
			nativeDesc.DepthOrArraySize = static_cast<UINT16>(desc.m_Extent.m_Depth);
			break;
		}

		return CD3DX12_RESOURCE_DESC(nativeDesc);
	}

	CD3DX12_RESOURCE_DESC ToD3D12ResourceDesc(const RHIBufferDesc& desc) noexcept
	{
		return CD3DX12_RESOURCE_DESC::Buffer(
			static_cast<UINT64>(desc.m_SizeInBytes),
			ToD3D12ResourceFlags(desc.m_Usage));
	}
}
