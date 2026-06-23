#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12RHITypeUtils.h"

namespace gglab
{
	DXGI_FORMAT ToDXGIFormat(RHIFormat format) noexcept
	{
		switch (format)
		{
		case RHIFormat::Unknown:
			return DXGI_FORMAT_UNKNOWN;
		case RHIFormat::R8G8B8A8Unorm:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case RHIFormat::R16G16Float:
			return DXGI_FORMAT_R16G16_FLOAT;
		case RHIFormat::R16G16B16A16Float:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case RHIFormat::R32Typeless:
			return DXGI_FORMAT_R32_TYPELESS;
		case RHIFormat::R32Float:
			return DXGI_FORMAT_R32_FLOAT;
		case RHIFormat::D24UnormS8Uint:
			return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case RHIFormat::D32Float:
			return DXGI_FORMAT_D32_FLOAT;
		}

		GGLAB_UNREACHABLE("Unhandled RHIFormat.");
	}

	RHIFormat ToRHIFormat(DXGI_FORMAT format) noexcept
	{
		switch (format)
		{
		case DXGI_FORMAT_UNKNOWN:
			return RHIFormat::Unknown;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			return RHIFormat::R8G8B8A8Unorm;
		case DXGI_FORMAT_R16G16_FLOAT:
			return RHIFormat::R16G16Float;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return RHIFormat::R16G16B16A16Float;
		case DXGI_FORMAT_R32_TYPELESS:
			return RHIFormat::R32Typeless;
		case DXGI_FORMAT_R32_FLOAT:
			return RHIFormat::R32Float;
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
			return RHIFormat::D24UnormS8Uint;
		case DXGI_FORMAT_D32_FLOAT:
			return RHIFormat::D32Float;
		default:
			GGLAB_ASSERT_MSG(false, "Unsupported DXGI_FORMAT for RHIFormat conversion.");
			return RHIFormat::Unknown;
		}
	}

	D3D12_COMMAND_LIST_TYPE ToD3D12CommandListType(RHIQueueType queueType) noexcept
	{
		switch (queueType)
		{
		case RHIQueueType::Graphics:
			return D3D12_COMMAND_LIST_TYPE_DIRECT;
		case RHIQueueType::Compute:
			return D3D12_COMMAND_LIST_TYPE_COMPUTE;
		case RHIQueueType::Copy:
			return D3D12_COMMAND_LIST_TYPE_COPY;
		}

		GGLAB_UNREACHABLE("Unhandled RHIQueueType.");
	}

	D3D12_BARRIER_ACCESS ToD3D12BarrierAccess(RHIAccess access) noexcept
	{
		if (access == RHIAccess::None)
		{
			return D3D12_BARRIER_ACCESS_NO_ACCESS;
		}

		D3D12_BARRIER_ACCESS barrierAccess = D3D12_BARRIER_ACCESS_COMMON;
		if (Test(access, RHIAccess::ShaderResource))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
		}
		if (Test(access, RHIAccess::RenderTarget))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
		}
		if (Test(access, RHIAccess::DepthStencilRead))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
		}
		if (Test(access, RHIAccess::DepthStencilWrite))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
		}
		if (Test(access, RHIAccess::UnorderedAccess))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
		}
		if (Test(access, RHIAccess::CopySource))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
		}
		if (Test(access, RHIAccess::CopyDest))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_COPY_DEST;
		}
		if (Test(access, RHIAccess::VertexBuffer))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
		}
		if (Test(access, RHIAccess::IndexBuffer))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
		}
		if (Test(access, RHIAccess::ConstantBuffer))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
		}
		if (Test(access, RHIAccess::IndirectArgument))
		{
			barrierAccess |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
		}

		return barrierAccess;
	}

	D3D12_BARRIER_LAYOUT ToD3D12BarrierLayout(RHILayout layout) noexcept
	{
		switch (layout)
		{
		case RHILayout::Unknown:
			return D3D12_BARRIER_LAYOUT_UNDEFINED;
		case RHILayout::Common:
			return D3D12_BARRIER_LAYOUT_COMMON;
		case RHILayout::ShaderResource:
			return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
		case RHILayout::RenderTarget:
			return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
		case RHILayout::DepthStencilRead:
			return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
		case RHILayout::DepthStencilWrite:
			return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
		case RHILayout::UnorderedAccess:
			return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
		case RHILayout::CopySource:
			return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
		case RHILayout::CopyDest:
			return D3D12_BARRIER_LAYOUT_COPY_DEST;
		case RHILayout::Present:
			return D3D12_BARRIER_LAYOUT_PRESENT;
		}

		GGLAB_UNREACHABLE("Unhandled RHILayout.");
	}

	D3D12_RESOURCE_STATES ToD3D12ResourceStates(RHIAccess access) noexcept
	{
		if (access == RHIAccess::None || access == RHIAccess::Common)
		{
			return D3D12_RESOURCE_STATE_COMMON;
		}

		if (Test(access, RHIAccess::Present))
		{
			GGLAB_ASSERT_MSG(access == RHIAccess::Present, "RHIAccess::Present must be exclusive.");
			return D3D12_RESOURCE_STATE_PRESENT;
		}

		D3D12_RESOURCE_STATES states = D3D12_RESOURCE_STATE_COMMON;
		if (Test(access, RHIAccess::ShaderResource))
		{
			states |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		}
		if (Test(access, RHIAccess::RenderTarget))
		{
			states |= D3D12_RESOURCE_STATE_RENDER_TARGET;
		}
		if (Test(access, RHIAccess::DepthStencilRead))
		{
			states |= D3D12_RESOURCE_STATE_DEPTH_READ;
		}
		if (Test(access, RHIAccess::DepthStencilWrite))
		{
			states |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
		}
		if (Test(access, RHIAccess::UnorderedAccess))
		{
			states |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}
		if (Test(access, RHIAccess::CopySource))
		{
			states |= D3D12_RESOURCE_STATE_COPY_SOURCE;
		}
		if (Test(access, RHIAccess::CopyDest))
		{
			states |= D3D12_RESOURCE_STATE_COPY_DEST;
		}
		if (Test(access, RHIAccess::VertexBuffer | RHIAccess::ConstantBuffer))
		{
			states |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		}
		if (Test(access, RHIAccess::IndexBuffer))
		{
			states |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
		}
		if (Test(access, RHIAccess::IndirectArgument))
		{
			states |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
		}

		return states;
	}

	D3D12_RESOURCE_STATES ToD3D12ResourceStates(RHIResourceState state) noexcept
	{
		return ToD3D12ResourceStates(state.m_Access);
	}

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
