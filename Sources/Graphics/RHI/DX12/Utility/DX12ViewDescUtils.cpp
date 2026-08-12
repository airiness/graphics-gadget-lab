#include "Graphics/RHI/DX12/Utility/DX12ViewDescUtils.h"
#include "Core/CoreMacros.h"
#include "Core/Utility/MathUtils.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHISubresourceUtils.h"
#include "Graphics/Utility/DXGIFormatUtils.h"

#include <algorithm>
#include <cstdint>

namespace gglab
{
	namespace
	{
		DXGI_FORMAT ResolveViewFormat(
			RHIFormat format, const D3D12_RESOURCE_DESC& resourceDesc) noexcept
		{
			return format == RHIFormat::Unknown ? resourceDesc.Format : ToDXGIFormat(format);
		}

		UINT ResolveMipLevels(uint32_t mipLevels) noexcept
		{
			return mipLevels == RHISubresourceRange::Remaining ? static_cast<UINT>(-1)
				: static_cast<UINT>(mipLevels);
		}

		uint32_t ResolvePlaneSlice(RHITextureAspect aspects) noexcept
		{
			return Test(aspects, RHITextureAspect::Stencil) &&
				!Test(aspects, RHITextureAspect::Depth)
				? 1u
				: 0u;
		}

		uint32_t ResolveArraySliceCount(
			const RHITextureViewDesc& desc, const D3D12_RESOURCE_DESC& resourceDesc) noexcept
		{
			return ResolveSubresourceCount(desc.m_Subresources.m_BaseArraySlice,
				desc.m_Subresources.m_ArraySliceCount,
				static_cast<uint32_t>(resourceDesc.DepthOrArraySize));
		}

		uint32_t ResolveCubeCount(
			const RHITextureViewDesc& desc, const D3D12_RESOURCE_DESC& resourceDesc) noexcept
		{
			return ResolveArraySliceCount(desc, resourceDesc) / CubemapFaceCount;
		}

		RHITextureViewDimension ResolveTextureViewDimension(
			RHITextureViewDimension dimension, const D3D12_RESOURCE_DESC& resourceDesc) noexcept
		{
			if (dimension != RHITextureViewDimension::Unknown)
			{
				return dimension;
			}

			switch (resourceDesc.Dimension)
			{
			case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
				return resourceDesc.DepthOrArraySize > 1 ? RHITextureViewDimension::Texture1DArray
					: RHITextureViewDimension::Texture1D;
			case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
				return resourceDesc.DepthOrArraySize > 1 ? RHITextureViewDimension::Texture2DArray
					: RHITextureViewDimension::Texture2D;
			case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
				return RHITextureViewDimension::Texture3D;
			case D3D12_RESOURCE_DIMENSION_UNKNOWN:
			case D3D12_RESOURCE_DIMENSION_BUFFER:
				GGLAB_UNREACHABLE(
					"Cannot infer a texture view dimension from a non-texture resource.");
			}
			return RHITextureViewDimension::Unknown;
		}

		uint64_t ResolveBufferViewSize(
			uint64_t offsetInBytes, uint64_t sizeInBytes, uint64_t bufferSizeInBytes) noexcept
		{
			if (offsetInBytes >= bufferSizeInBytes)
			{
				return 0;
			}

			const uint64_t remainingSize = bufferSizeInBytes - offsetInBytes;
			return sizeInBytes == 0 ? remainingSize : std::min(sizeInBytes, remainingSize);
		}

		uint32_t ResolveBufferElementStride(const RHIBufferViewDesc& desc) noexcept
		{
			if (desc.m_StrideInBytes != 0)
			{
				return desc.m_StrideInBytes;
			}

			return std::max<uint32_t>(1, GetRHIFormatInfo(desc.m_Format).m_BytesPerBlock);
		}

	}

	D3D12_RENDER_TARGET_VIEW_DESC BuildD3D12RenderTargetViewDesc(
		const RHITextureViewDesc& desc, const D3D12_RESOURCE_DESC& resourceDesc) noexcept
	{
		D3D12_RENDER_TARGET_VIEW_DESC nativeDesc{};
		nativeDesc.Format = ResolveViewFormat(desc.m_Format, resourceDesc);

		const RHITextureViewDimension dimension =
			ResolveTextureViewDimension(desc.m_Dimension, resourceDesc);

		switch (dimension)
		{
		case RHITextureViewDimension::Texture1D:
			nativeDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
			nativeDesc.Texture1D.MipSlice = desc.m_Subresources.m_BaseMip;
			break;
		case RHITextureViewDimension::Texture1DArray:
			nativeDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
			nativeDesc.Texture1DArray.MipSlice = desc.m_Subresources.m_BaseMip;
			nativeDesc.Texture1DArray.FirstArraySlice = desc.m_Subresources.m_BaseArraySlice;
			nativeDesc.Texture1DArray.ArraySize = ResolveArraySliceCount(desc, resourceDesc);
			break;
		case RHITextureViewDimension::Texture2D:
			nativeDesc.ViewDimension = resourceDesc.SampleDesc.Count > 1
				? D3D12_RTV_DIMENSION_TEXTURE2DMS
				: D3D12_RTV_DIMENSION_TEXTURE2D;
			if (nativeDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2D)
			{
				nativeDesc.Texture2D.MipSlice = desc.m_Subresources.m_BaseMip;
				nativeDesc.Texture2D.PlaneSlice = ResolvePlaneSlice(desc.m_Subresources.m_Aspects);
			}
			break;
		case RHITextureViewDimension::Texture2DArray:
		case RHITextureViewDimension::TextureCube:
		case RHITextureViewDimension::TextureCubeArray:
			nativeDesc.ViewDimension = resourceDesc.SampleDesc.Count > 1
				? D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY
				: D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			if (nativeDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DARRAY)
			{
				nativeDesc.Texture2DArray.MipSlice = desc.m_Subresources.m_BaseMip;
				nativeDesc.Texture2DArray.FirstArraySlice = desc.m_Subresources.m_BaseArraySlice;
				nativeDesc.Texture2DArray.ArraySize = ResolveArraySliceCount(desc, resourceDesc);
				nativeDesc.Texture2DArray.PlaneSlice =
					ResolvePlaneSlice(desc.m_Subresources.m_Aspects);
			}
			else
			{
				nativeDesc.Texture2DMSArray.FirstArraySlice = desc.m_Subresources.m_BaseArraySlice;
				nativeDesc.Texture2DMSArray.ArraySize = ResolveArraySliceCount(desc, resourceDesc);
			}
			break;
		case RHITextureViewDimension::Texture3D:
			nativeDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
			nativeDesc.Texture3D.MipSlice = desc.m_Subresources.m_BaseMip;
			nativeDesc.Texture3D.FirstWSlice = desc.m_Subresources.m_BaseArraySlice;
			nativeDesc.Texture3D.WSize = ResolveArraySliceCount(desc, resourceDesc);
			break;
		case RHITextureViewDimension::Unknown:
			GGLAB_UNREACHABLE("Unexpected unknown RHI texture view dimension.");
		}

		return nativeDesc;
	}

	D3D12_DEPTH_STENCIL_VIEW_DESC BuildD3D12DepthStencilViewDesc(
		const RHITextureViewDesc& desc, const D3D12_RESOURCE_DESC& resourceDesc) noexcept
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC nativeDesc{};
		nativeDesc.Format = ResolveViewFormat(desc.m_Format, resourceDesc);
		if (desc.m_ReadOnlyDepth)
		{
			nativeDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
		}
		if (desc.m_ReadOnlyStencil)
		{
			nativeDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
		}

		const RHITextureViewDimension dimension =
			ResolveTextureViewDimension(desc.m_Dimension, resourceDesc);

		switch (dimension)
		{
		case RHITextureViewDimension::Texture1D:
			nativeDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
			nativeDesc.Texture1D.MipSlice = desc.m_Subresources.m_BaseMip;
			break;
		case RHITextureViewDimension::Texture1DArray:
			nativeDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
			nativeDesc.Texture1DArray.MipSlice = desc.m_Subresources.m_BaseMip;
			nativeDesc.Texture1DArray.FirstArraySlice = desc.m_Subresources.m_BaseArraySlice;
			nativeDesc.Texture1DArray.ArraySize = ResolveArraySliceCount(desc, resourceDesc);
			break;
		case RHITextureViewDimension::Texture2D:
			nativeDesc.ViewDimension = resourceDesc.SampleDesc.Count > 1
				? D3D12_DSV_DIMENSION_TEXTURE2DMS
				: D3D12_DSV_DIMENSION_TEXTURE2D;
			if (nativeDesc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2D)
			{
				nativeDesc.Texture2D.MipSlice = desc.m_Subresources.m_BaseMip;
			}
			break;
		case RHITextureViewDimension::Texture2DArray:
		case RHITextureViewDimension::TextureCube:
		case RHITextureViewDimension::TextureCubeArray:
			nativeDesc.ViewDimension = resourceDesc.SampleDesc.Count > 1
				? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY
				: D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			if (nativeDesc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DARRAY)
			{
				nativeDesc.Texture2DArray.MipSlice = desc.m_Subresources.m_BaseMip;
				nativeDesc.Texture2DArray.FirstArraySlice = desc.m_Subresources.m_BaseArraySlice;
				nativeDesc.Texture2DArray.ArraySize = ResolveArraySliceCount(desc, resourceDesc);
			}
			else
			{
				nativeDesc.Texture2DMSArray.FirstArraySlice = desc.m_Subresources.m_BaseArraySlice;
				nativeDesc.Texture2DMSArray.ArraySize = ResolveArraySliceCount(desc, resourceDesc);
			}
			break;
		case RHITextureViewDimension::Texture3D:
			GGLAB_UNREACHABLE("D3D12 depth-stencil views do not support 3D textures.");
			break;
		case RHITextureViewDimension::Unknown:
			GGLAB_UNREACHABLE("Unexpected unknown RHI texture view dimension.");
			break;
		}

		return nativeDesc;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC BuildD3D12ShaderResourceViewDesc(
		const RHITextureViewDesc& desc, const D3D12_RESOURCE_DESC& resourceDesc) noexcept
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC nativeDesc{};
		nativeDesc.Format = ResolveViewFormat(desc.m_Format, resourceDesc);
		nativeDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		const RHITextureViewDimension dimension =
			ResolveTextureViewDimension(desc.m_Dimension, resourceDesc);

		switch (dimension)
		{
		case RHITextureViewDimension::Texture1D:
			nativeDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
			nativeDesc.Texture1D.MostDetailedMip = desc.m_Subresources.m_BaseMip;
			nativeDesc.Texture1D.MipLevels = ResolveMipLevels(desc.m_Subresources.m_MipCount);
			nativeDesc.Texture1D.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			break;
		case RHITextureViewDimension::Texture1DArray:
			nativeDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
			nativeDesc.Texture1DArray.MostDetailedMip = desc.m_Subresources.m_BaseMip;
			nativeDesc.Texture1DArray.MipLevels = ResolveMipLevels(desc.m_Subresources.m_MipCount);
			nativeDesc.Texture1DArray.FirstArraySlice = desc.m_Subresources.m_BaseArraySlice;
			nativeDesc.Texture1DArray.ArraySize = ResolveArraySliceCount(desc, resourceDesc);
			nativeDesc.Texture1DArray.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			break;
		case RHITextureViewDimension::Texture2D:
			nativeDesc.ViewDimension = resourceDesc.SampleDesc.Count > 1
				? D3D12_SRV_DIMENSION_TEXTURE2DMS
				: D3D12_SRV_DIMENSION_TEXTURE2D;
			if (nativeDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D)
			{
				nativeDesc.Texture2D.MostDetailedMip = desc.m_Subresources.m_BaseMip;
				nativeDesc.Texture2D.MipLevels = ResolveMipLevels(desc.m_Subresources.m_MipCount);
				nativeDesc.Texture2D.PlaneSlice = ResolvePlaneSlice(desc.m_Subresources.m_Aspects);
				nativeDesc.Texture2D.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			}
			break;
		case RHITextureViewDimension::Texture2DArray:
			nativeDesc.ViewDimension = resourceDesc.SampleDesc.Count > 1
				? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY
				: D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			if (nativeDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY)
			{
				nativeDesc.Texture2DArray.MostDetailedMip = desc.m_Subresources.m_BaseMip;
				nativeDesc.Texture2DArray.MipLevels =
					ResolveMipLevels(desc.m_Subresources.m_MipCount);
				nativeDesc.Texture2DArray.FirstArraySlice = desc.m_Subresources.m_BaseArraySlice;
				nativeDesc.Texture2DArray.ArraySize = ResolveArraySliceCount(desc, resourceDesc);
				nativeDesc.Texture2DArray.PlaneSlice =
					ResolvePlaneSlice(desc.m_Subresources.m_Aspects);
				nativeDesc.Texture2DArray.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			}
			else
			{
				nativeDesc.Texture2DMSArray.FirstArraySlice = desc.m_Subresources.m_BaseArraySlice;
				nativeDesc.Texture2DMSArray.ArraySize = ResolveArraySliceCount(desc, resourceDesc);
			}
			break;
		case RHITextureViewDimension::Texture3D:
			nativeDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			nativeDesc.Texture3D.MostDetailedMip = desc.m_Subresources.m_BaseMip;
			nativeDesc.Texture3D.MipLevels = ResolveMipLevels(desc.m_Subresources.m_MipCount);
			nativeDesc.Texture3D.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			break;
		case RHITextureViewDimension::TextureCube:
			nativeDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			nativeDesc.TextureCube.MostDetailedMip = desc.m_Subresources.m_BaseMip;
			nativeDesc.TextureCube.MipLevels = ResolveMipLevels(desc.m_Subresources.m_MipCount);
			nativeDesc.TextureCube.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			break;
		case RHITextureViewDimension::TextureCubeArray:
			nativeDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
			nativeDesc.TextureCubeArray.MostDetailedMip = desc.m_Subresources.m_BaseMip;
			nativeDesc.TextureCubeArray.MipLevels =
				ResolveMipLevels(desc.m_Subresources.m_MipCount);
			nativeDesc.TextureCubeArray.First2DArrayFace = desc.m_Subresources.m_BaseArraySlice;
			nativeDesc.TextureCubeArray.NumCubes = ResolveCubeCount(desc, resourceDesc);
			nativeDesc.TextureCubeArray.ResourceMinLODClamp = desc.m_ResourceMinLODClamp;
			break;
		case RHITextureViewDimension::Unknown:
			GGLAB_UNREACHABLE("Unexpected unknown RHI texture view dimension.");
		}

		return nativeDesc;
	}

	D3D12_UNORDERED_ACCESS_VIEW_DESC BuildD3D12UnorderedAccessViewDesc(
		const RHITextureViewDesc& desc, const D3D12_RESOURCE_DESC& resourceDesc) noexcept
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC nativeDesc{};
		nativeDesc.Format = ResolveViewFormat(desc.m_Format, resourceDesc);

		const RHITextureViewDimension dimension =
			ResolveTextureViewDimension(desc.m_Dimension, resourceDesc);

		switch (dimension)
		{
		case RHITextureViewDimension::Texture1D:
			nativeDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
			nativeDesc.Texture1D.MipSlice = desc.m_Subresources.m_BaseMip;
			break;
		case RHITextureViewDimension::Texture1DArray:
			nativeDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
			nativeDesc.Texture1DArray.MipSlice = desc.m_Subresources.m_BaseMip;
			nativeDesc.Texture1DArray.FirstArraySlice = desc.m_Subresources.m_BaseArraySlice;
			nativeDesc.Texture1DArray.ArraySize = ResolveArraySliceCount(desc, resourceDesc);
			break;
		case RHITextureViewDimension::Texture2D:
			nativeDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			nativeDesc.Texture2D.MipSlice = desc.m_Subresources.m_BaseMip;
			nativeDesc.Texture2D.PlaneSlice = ResolvePlaneSlice(desc.m_Subresources.m_Aspects);
			break;
		case RHITextureViewDimension::Texture2DArray:
		case RHITextureViewDimension::TextureCube:
		case RHITextureViewDimension::TextureCubeArray:
			nativeDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			nativeDesc.Texture2DArray.MipSlice = desc.m_Subresources.m_BaseMip;
			nativeDesc.Texture2DArray.FirstArraySlice = desc.m_Subresources.m_BaseArraySlice;
			nativeDesc.Texture2DArray.ArraySize = ResolveArraySliceCount(desc, resourceDesc);
			nativeDesc.Texture2DArray.PlaneSlice = ResolvePlaneSlice(desc.m_Subresources.m_Aspects);
			break;
		case RHITextureViewDimension::Texture3D:
			nativeDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
			nativeDesc.Texture3D.MipSlice = desc.m_Subresources.m_BaseMip;
			nativeDesc.Texture3D.FirstWSlice = desc.m_Subresources.m_BaseArraySlice;
			nativeDesc.Texture3D.WSize = ResolveArraySliceCount(desc, resourceDesc);
			break;
		case RHITextureViewDimension::Unknown:
			GGLAB_UNREACHABLE("Unexpected unknown RHI texture view dimension.");
		}

		return nativeDesc;
	}

	D3D12_CONSTANT_BUFFER_VIEW_DESC BuildD3D12ConstantBufferViewDesc(const RHIBufferViewDesc& desc,
		D3D12_GPU_VIRTUAL_ADDRESS baseGpuAddress, uint64_t bufferSizeInBytes) noexcept
	{
		const uint64_t sizeInBytes =
			ResolveBufferViewSize(desc.m_OffsetInBytes, desc.m_SizeInBytes, bufferSizeInBytes);

		D3D12_CONSTANT_BUFFER_VIEW_DESC nativeDesc{};
		nativeDesc.BufferLocation = baseGpuAddress + desc.m_OffsetInBytes;
		nativeDesc.SizeInBytes = static_cast<UINT>(utils::AlignUp(
			sizeInBytes, static_cast<uint64_t>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));
		return nativeDesc;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC BuildD3D12ShaderResourceViewDesc(
		const RHIBufferViewDesc& desc, uint64_t bufferSizeInBytes) noexcept
	{
		const uint64_t sizeInBytes =
			ResolveBufferViewSize(desc.m_OffsetInBytes, desc.m_SizeInBytes, bufferSizeInBytes);
		const uint32_t stride = ResolveBufferElementStride(desc);

		D3D12_SHADER_RESOURCE_VIEW_DESC nativeDesc{};
		nativeDesc.Format =
			desc.m_StrideInBytes == 0 ? ToDXGIFormat(desc.m_Format) : DXGI_FORMAT_UNKNOWN;
		nativeDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		nativeDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		nativeDesc.Buffer.FirstElement = desc.m_OffsetInBytes / stride;
		nativeDesc.Buffer.NumElements = static_cast<UINT>(sizeInBytes / stride);
		nativeDesc.Buffer.StructureByteStride = desc.m_StrideInBytes;
		nativeDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		return nativeDesc;
	}

	D3D12_UNORDERED_ACCESS_VIEW_DESC BuildD3D12UnorderedAccessViewDesc(
		const RHIBufferViewDesc& desc, uint64_t bufferSizeInBytes) noexcept
	{
		const uint64_t sizeInBytes =
			ResolveBufferViewSize(desc.m_OffsetInBytes, desc.m_SizeInBytes, bufferSizeInBytes);
		const uint32_t stride = ResolveBufferElementStride(desc);

		D3D12_UNORDERED_ACCESS_VIEW_DESC nativeDesc{};
		nativeDesc.Format =
			desc.m_StrideInBytes == 0 ? ToDXGIFormat(desc.m_Format) : DXGI_FORMAT_UNKNOWN;
		nativeDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		nativeDesc.Buffer.FirstElement = desc.m_OffsetInBytes / stride;
		nativeDesc.Buffer.NumElements = static_cast<UINT>(sizeInBytes / stride);
		nativeDesc.Buffer.StructureByteStride = desc.m_StrideInBytes;
		nativeDesc.Buffer.CounterOffsetInBytes = 0;
		nativeDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
		return nativeDesc;
	}
}
