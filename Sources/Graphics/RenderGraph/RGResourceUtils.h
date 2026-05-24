#pragma once
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/DX12/DX12Texture.h"

namespace gglab
{
	template<typename ResourceUsage>
	constexpr inline D3D12_RESOURCE_STATES ToD3D12ResourceStates(ResourceUsage usage, bool unuse = false) noexcept = delete;

	template<>
	constexpr inline D3D12_RESOURCE_STATES ToD3D12ResourceStates<RGTextureUsage>(RGTextureUsage rgTexUsage, bool depthReadOnly) noexcept
	{
		using U = RGTextureUsage;
		D3D12_RESOURCE_STATES states = D3D12_RESOURCE_STATE_COMMON;

		if (Test(rgTexUsage, U::Sample))
		{
			states |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		}
		if (Test(rgTexUsage, U::RenderTarget))
		{
			states |= D3D12_RESOURCE_STATE_RENDER_TARGET;
		}
		if (Test(rgTexUsage, U::DepthStencil))
		{
			states |= depthReadOnly ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE;
		}
		if (Test(rgTexUsage, U::UAV))
		{
			states |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}
		if (Test(rgTexUsage, U::CopySrc))
		{
			states |= D3D12_RESOURCE_STATE_COPY_SOURCE;
		}
		if (Test(rgTexUsage, U::CopyDst))
		{
			states |= D3D12_RESOURCE_STATE_COPY_DEST;
		}
		if (Test(rgTexUsage, U::Present))
		{
			GGLAB_ASSERT_MSG(rgTexUsage == U::Present, "RGTextureUsage::Present must be exclusive.");
			return D3D12_RESOURCE_STATE_PRESENT;
		}

		return states;
	}

	template<>
	constexpr inline D3D12_RESOURCE_STATES ToD3D12ResourceStates<RGBufferUsage>(RGBufferUsage rgBufUsage, bool) noexcept
	{
		using U = RGBufferUsage;
		D3D12_RESOURCE_STATES states = D3D12_RESOURCE_STATE_COMMON;

		if (Test(rgBufUsage, U::Vertex | U::Constant))
		{
			states |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		}
		if (Test(rgBufUsage, U::Index))
		{
			states |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
		}
		if (Test(rgBufUsage, U::UAV))
		{
			states |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}
		if (Test(rgBufUsage, U::CopySrc))
		{
			states |= D3D12_RESOURCE_STATE_COPY_SOURCE;
		}
		if (Test(rgBufUsage, U::CopyDst))
		{
			states |= D3D12_RESOURCE_STATE_COPY_DEST;
		}

		return states;
	}

	// Get default clear value from RGResourceDesc. Texture can be use when render target or depth stencil.
	template<typename RGResourceDesc>
	constexpr inline std::optional<D3D12_CLEAR_VALUE> DefaultClearValue(const RGResourceDesc&) noexcept = delete;

	template<>
	constexpr inline std::optional<D3D12_CLEAR_VALUE> DefaultClearValue<RGTextureDesc>(const RGTextureDesc& rgTexDesc) noexcept
	{
		if (Test(rgTexDesc.m_Usage, RGTextureUsage::RenderTarget))
		{
			D3D12_CLEAR_VALUE clearValue{};
			clearValue.Format = rgTexDesc.m_Format;
			clearValue.Color[0] = 0.0f;
			clearValue.Color[1] = 0.0f;
			clearValue.Color[2] = 0.0f;
			clearValue.Color[3] = 1.0f;
			return std::optional<D3D12_CLEAR_VALUE>(clearValue);
		}
		else if (Test(rgTexDesc.m_Usage, RGTextureUsage::DepthStencil))
		{
			D3D12_CLEAR_VALUE clearValue{};
			clearValue.Format = rgTexDesc.m_Format;
			clearValue.DepthStencil.Depth = 1.0f;
			clearValue.DepthStencil.Stencil = 0;
			return std::optional<D3D12_CLEAR_VALUE>(clearValue);
		}

		return std::nullopt;
	}

	// Buffer do not need default clear value.
	template<>
	constexpr inline std::optional<D3D12_CLEAR_VALUE> DefaultClearValue<RGBufferDesc>(const RGBufferDesc&) noexcept
	{
		return std::nullopt;
	}

	constexpr inline D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(RGTextureUsage rgTexUsage) noexcept
	{
		return
			(Test(rgTexUsage, RGTextureUsage::RenderTarget) ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE) |
			(Test(rgTexUsage, RGTextureUsage::DepthStencil) ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_NONE) |
			(Test(rgTexUsage, RGTextureUsage::UAV) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
	}

	inline CD3DX12_RESOURCE_DESC ToD3D12ResourceDesc(const RGTextureDesc& rgTexDesc) noexcept
	{
		D3D12_RESOURCE_FLAGS flags = ToD3D12ResourceFlags(rgTexDesc.m_Usage);
		return CD3DX12_RESOURCE_DESC::Tex2D(
			rgTexDesc.m_Format,
			static_cast<UINT64>(rgTexDesc.m_Width),
			static_cast<UINT>(rgTexDesc.m_Height),
			static_cast<UINT16>(rgTexDesc.m_ArraySize),
			static_cast<UINT16>(rgTexDesc.m_MipLevels),
			static_cast<UINT>(rgTexDesc.m_SampleCount),
			0,
			flags);
	}

	constexpr inline D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(RGBufferUsage rgBufUsage) noexcept
	{
		return Test(rgBufUsage, RGBufferUsage::UAV) ?
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS :
			D3D12_RESOURCE_FLAG_NONE;
	}

	inline CD3DX12_RESOURCE_DESC ToD3D12ResourceDesc(const RGBufferDesc& rgBufDesc) noexcept
	{
		D3D12_RESOURCE_FLAGS flags = ToD3D12ResourceFlags(rgBufDesc.m_Usage);
		return CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(rgBufDesc.m_SizeInBytes), flags);
	}

	inline RGTextureDesc ToRGTextureDesc(const D3D12_RESOURCE_DESC& nativeDesc, RGTextureUsage usage) noexcept
	{
		RGTextureDesc rgDesc{};
		rgDesc.m_Width = static_cast<uint32_t>(nativeDesc.Width);
		rgDesc.m_Height = static_cast<uint32_t>(nativeDesc.Height);
		rgDesc.m_ArraySize = static_cast<uint16_t>(nativeDesc.DepthOrArraySize);
		rgDesc.m_MipLevels = static_cast<uint16_t>(nativeDesc.MipLevels);
		rgDesc.m_SampleCount = static_cast<uint16_t>(nativeDesc.SampleDesc.Count);
		rgDesc.m_Format = nativeDesc.Format;
		rgDesc.m_Usage = usage;
		return rgDesc;
	}

	inline RGTextureDesc ToRGTextureDesc(const DX12Texture& texture, RGTextureUsage usage) noexcept
	{
		return ToRGTextureDesc(texture.GetDesc(), usage);
	}
}
