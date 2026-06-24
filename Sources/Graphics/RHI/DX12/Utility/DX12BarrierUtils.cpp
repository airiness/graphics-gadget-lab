#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"

namespace gglab
{
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
}
