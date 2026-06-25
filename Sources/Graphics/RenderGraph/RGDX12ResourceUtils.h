#pragma once
#include "Graphics/RenderGraph/RGResourceUtils.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/Utility/DX12FormatUtils.h"

namespace gglab
{
	constexpr inline D3D12_BARRIER_SYNC ToD3D12BarrierSync(RGStage stage) noexcept
	{
		if (stage == RGStage::None)
		{
			return D3D12_BARRIER_SYNC_NONE;
		}

		D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;
		if (Test(stage, RGStage::All))
		{
			sync |= D3D12_BARRIER_SYNC_ALL;
		}
		if (Test(stage, RGStage::AllShading))
		{
			sync |= D3D12_BARRIER_SYNC_ALL_SHADING;
		}
		if (Test(stage, RGStage::VertexShading))
		{
			sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
		}
		if (Test(stage, RGStage::IndexInput))
		{
			sync |= D3D12_BARRIER_SYNC_INDEX_INPUT;
		}
		if (Test(stage, RGStage::RenderTarget))
		{
			sync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
		}
		if (Test(stage, RGStage::DepthStencil))
		{
			sync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
		}
		if (Test(stage, RGStage::Copy))
		{
			sync |= D3D12_BARRIER_SYNC_COPY;
		}
		return sync;
	}

	constexpr inline D3D12_BARRIER_ACCESS ToD3D12BarrierAccess(RGAccess access) noexcept
	{
		if (access == RGAccess::None)
		{
			return D3D12_BARRIER_ACCESS_NO_ACCESS;
		}

		D3D12_BARRIER_ACCESS barrierAccess = D3D12_BARRIER_ACCESS_COMMON;
		if (Test(access, RGAccess::ShaderResource))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
		}
		if (Test(access, RGAccess::RenderTarget))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
		}
		if (Test(access, RGAccess::DepthStencilRead))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
		}
		if (Test(access, RGAccess::DepthStencilWrite))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
		}
		if (Test(access, RGAccess::UnorderedAccess))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
		}
		if (Test(access, RGAccess::CopySource))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
		}
		if (Test(access, RGAccess::CopyDest))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_COPY_DEST;
		}
		if (Test(access, RGAccess::VertexBuffer))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
		}
		if (Test(access, RGAccess::IndexBuffer))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
		}
		if (Test(access, RGAccess::ConstantBuffer))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
		}
		return barrierAccess;
	}

	constexpr inline D3D12_BARRIER_LAYOUT ToD3D12BarrierLayout(RGLayout layout) noexcept
	{
		switch (layout)
		{
		case RGLayout::Common:
			return D3D12_BARRIER_LAYOUT_COMMON;
		case RGLayout::ShaderResource:
			return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
		case RGLayout::RenderTarget:
			return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
		case RGLayout::DepthStencilRead:
			return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
		case RGLayout::DepthStencilWrite:
			return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
		case RGLayout::UnorderedAccess:
			return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
		case RGLayout::CopySource:
			return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
		case RGLayout::CopyDest:
			return D3D12_BARRIER_LAYOUT_COPY_DEST;
		case RGLayout::Present:
			return D3D12_BARRIER_LAYOUT_PRESENT;
		}

		GGLAB_UNREACHABLE("Unhandled RGLayout.");
	}

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
		if (Test(rgTexUsage, U::DepthStencilRead))
		{
			states |= D3D12_RESOURCE_STATE_DEPTH_READ;
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

	template<typename RGResourceDesc>
	inline std::optional<D3D12_CLEAR_VALUE> DefaultClearValue(const RGResourceDesc&) noexcept = delete;

	template<>
	inline std::optional<D3D12_CLEAR_VALUE> DefaultClearValue<RGTextureDesc>(const RGTextureDesc& rgTexDesc) noexcept
	{
		if (Test(rgTexDesc.m_Usage, RGTextureUsage::RenderTarget))
		{
			D3D12_CLEAR_VALUE clearValue{};
			clearValue.Format = ToDXGIFormat(rgTexDesc.m_Format);
			clearValue.Color[0] = 0.0f;
			clearValue.Color[1] = 0.0f;
			clearValue.Color[2] = 0.0f;
			clearValue.Color[3] = 1.0f;
			return std::optional<D3D12_CLEAR_VALUE>(clearValue);
		}
		else if (Test(rgTexDesc.m_Usage, RGTextureUsage::DepthStencil))
		{
			D3D12_CLEAR_VALUE clearValue{};
			clearValue.Format = (rgTexDesc.m_Format == RHIFormat::R32Typeless) ?
				DXGI_FORMAT_D32_FLOAT :
				ToDXGIFormat(rgTexDesc.m_Format);
			clearValue.DepthStencil.Depth = 1.0f;
			clearValue.DepthStencil.Stencil = 0;
			return std::optional<D3D12_CLEAR_VALUE>(clearValue);
		}

		return std::nullopt;
	}

	template<>
	inline std::optional<D3D12_CLEAR_VALUE> DefaultClearValue<RGBufferDesc>(const RGBufferDesc&) noexcept
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
			ToDXGIFormat(rgTexDesc.m_Format),
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
		rgDesc.m_Format = ToRHIFormat(nativeDesc.Format);
		rgDesc.m_Usage = usage;
		return rgDesc;
	}

	inline RGTextureDesc ToRGTextureDesc(const DX12Texture& texture, RGTextureUsage usage) noexcept
	{
		return ToRGTextureDesc(texture.GetDesc(), usage);
	}

	inline D3D12_RENDER_TARGET_VIEW_DESC ToD3D12RenderTargetViewDesc(const RGTextureViewDesc& rgDesc) noexcept
	{
		D3D12_RENDER_TARGET_VIEW_DESC desc{};
		desc.Format = ToDXGIFormat(rgDesc.m_Format);

		switch (rgDesc.m_Dimension)
		{
		case RGTextureViewDimension::Texture2DArray:
			desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.MipSlice = rgDesc.m_MipSlice;
			desc.Texture2DArray.FirstArraySlice = rgDesc.m_FirstArraySlice;
			desc.Texture2DArray.ArraySize = rgDesc.m_ArraySize;
			desc.Texture2DArray.PlaneSlice = rgDesc.m_PlaneSlice;
			break;
		case RGTextureViewDimension::Unknown:
		case RGTextureViewDimension::Texture2D:
			desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = rgDesc.m_MipSlice;
			desc.Texture2D.PlaneSlice = rgDesc.m_PlaneSlice;
			break;
		default:
			GGLAB_UNREACHABLE("Unsupported RGTextureViewDimension for RTV.");
		}

		return desc;
	}

	inline D3D12_DEPTH_STENCIL_VIEW_DESC ToD3D12DepthStencilViewDesc(const RGTextureViewDesc& rgDesc) noexcept
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC desc{};
		desc.Format = ToDXGIFormat(rgDesc.m_Format);

		switch (rgDesc.m_Dimension)
		{
		case RGTextureViewDimension::Unknown:
		case RGTextureViewDimension::Texture2D:
			desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			desc.Flags = D3D12_DSV_FLAG_NONE;
			desc.Texture2D.MipSlice = rgDesc.m_MipSlice;
			break;
		default:
			GGLAB_UNREACHABLE("Unsupported RGTextureViewDimension for DSV.");
		}

		return desc;
	}

	inline D3D12_SHADER_RESOURCE_VIEW_DESC ToD3D12ShaderResourceViewDesc(const RGTextureViewDesc& rgDesc) noexcept
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
		desc.Format = ToDXGIFormat(rgDesc.m_Format);
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		const auto mipLevels =
			rgDesc.m_MipLevels == RGTextureViewDesc::AllMipLevels ?
			static_cast<UINT>(-1) :
			rgDesc.m_MipLevels;

		switch (rgDesc.m_Dimension)
		{
		case RGTextureViewDimension::Unknown:
		case RGTextureViewDimension::Texture2D:
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MostDetailedMip = rgDesc.m_MostDetailedMip;
			desc.Texture2D.MipLevels = mipLevels;
			desc.Texture2D.PlaneSlice = rgDesc.m_PlaneSlice;
			desc.Texture2D.ResourceMinLODClamp = rgDesc.m_ResourceMinLODClamp;
			break;
		case RGTextureViewDimension::Texture2DArray:
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.MostDetailedMip = rgDesc.m_MostDetailedMip;
			desc.Texture2DArray.MipLevels = mipLevels;
			desc.Texture2DArray.FirstArraySlice = rgDesc.m_FirstArraySlice;
			desc.Texture2DArray.ArraySize = rgDesc.m_ArraySize;
			desc.Texture2DArray.PlaneSlice = rgDesc.m_PlaneSlice;
			desc.Texture2DArray.ResourceMinLODClamp = rgDesc.m_ResourceMinLODClamp;
			break;
		case RGTextureViewDimension::TextureCube:
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			desc.TextureCube.MostDetailedMip = rgDesc.m_MostDetailedMip;
			desc.TextureCube.MipLevels = mipLevels;
			desc.TextureCube.ResourceMinLODClamp = rgDesc.m_ResourceMinLODClamp;
			break;
		case RGTextureViewDimension::TextureCubeArray:
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

	inline D3D12_UNORDERED_ACCESS_VIEW_DESC ToD3D12UnorderedAccessViewDesc(const RGTextureViewDesc& rgDesc) noexcept
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
		desc.Format = ToDXGIFormat(rgDesc.m_Format);

		switch (rgDesc.m_Dimension)
		{
		case RGTextureViewDimension::Unknown:
		case RGTextureViewDimension::Texture2D:
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = rgDesc.m_MipSlice;
			desc.Texture2D.PlaneSlice = rgDesc.m_PlaneSlice;
			break;
		case RGTextureViewDimension::Texture2DArray:
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.MipSlice = rgDesc.m_MipSlice;
			desc.Texture2DArray.FirstArraySlice = rgDesc.m_FirstArraySlice;
			desc.Texture2DArray.ArraySize = rgDesc.m_ArraySize;
			desc.Texture2DArray.PlaneSlice = rgDesc.m_PlaneSlice;
			break;
		default:
			GGLAB_UNREACHABLE("Unsupported RGTextureViewDimension for UAV.");
		}

		return desc;
	}
}
