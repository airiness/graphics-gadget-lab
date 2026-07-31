#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHISubresourceUtils.h"
#include "Graphics/Utility/DXGIFormatUtils.h"

namespace gglab
{
	namespace
	{
		struct DX12PlaneRange
		{
			uint32_t m_BasePlane = 0;
			uint32_t m_PlaneCount = 1;
		};

		DX12PlaneRange ToD3D12PlaneRange(RHITextureAspect aspects, uint32_t planeCount) noexcept
		{
			if (planeCount <= 1)
			{
				GGLAB_ASSERT_MSG(aspects != RHITextureAspect::Stencil,
					"Stencil subresource barrier requires a stencil plane.");
				return { 0, 1 };
			}

			if (Test(aspects, RHITextureAspect::Depth) && Test(aspects, RHITextureAspect::Stencil))
			{
				return { 0, std::min<uint32_t>(2, planeCount) };
			}
			if (Test(aspects, RHITextureAspect::Stencil))
			{
				return { 1, 1 };
			}
			return { 0, 1 };
		}

		uint32_t GetD3D12TextureArraySize(const D3D12_RESOURCE_DESC& desc) noexcept
		{
			if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
			{
				return 1;
			}
			return std::max<uint32_t>(1, desc.DepthOrArraySize);
		}

		uint32_t GetD3D12TexturePlaneCount(const D3D12_RESOURCE_DESC& desc) noexcept
		{
			const RHIFormat format = ToRHIFormat(desc.Format);
			const uint32_t planeCount = GetRHIFormatInfo(format).m_PlaneCount;
			return std::max<uint32_t>(1, planeCount);
		}

		D3D12_BARRIER_SUBRESOURCE_RANGE BuildD3D12BarrierSubresourceRange(
			const std::optional<RHISubresourceRange>& subresources,
			const D3D12_RESOURCE_DESC& resourceDesc) noexcept
		{
			if (!subresources)
			{
				return CD3DX12_BARRIER_SUBRESOURCE_RANGE(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
			}

			RHISubresourceRange range = *subresources;
			const uint32_t mipLevels = std::max<uint32_t>(1, resourceDesc.MipLevels);
			const uint32_t arraySize = GetD3D12TextureArraySize(resourceDesc);
			const uint32_t planeCount = GetD3D12TexturePlaneCount(resourceDesc);
			range.m_MipCount =
				ResolveSubresourceCount(range.m_BaseMip, range.m_MipCount, mipLevels);
			range.m_ArraySliceCount =
				ResolveSubresourceCount(range.m_BaseArraySlice, range.m_ArraySliceCount, arraySize);

			const DX12PlaneRange planeRange = ToD3D12PlaneRange(range.m_Aspects, planeCount);
			GGLAB_ASSERT_MSG(range.m_MipCount > 0 && range.m_ArraySliceCount > 0,
				"DX12 texture barrier received an empty subresource range.");
			GGLAB_ASSERT_MSG(planeRange.m_BasePlane + planeRange.m_PlaneCount <= planeCount,
				"DX12 texture barrier received an out-of-range plane selection.");
			if (range.m_MipCount == 0 || range.m_ArraySliceCount == 0 ||
				planeRange.m_BasePlane + planeRange.m_PlaneCount > planeCount)
			{
				return CD3DX12_BARRIER_SUBRESOURCE_RANGE(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
			}

			if (range.m_BaseMip == 0 && range.m_MipCount == mipLevels &&
				range.m_BaseArraySlice == 0 && range.m_ArraySliceCount == arraySize &&
				planeRange.m_BasePlane == 0 && planeRange.m_PlaneCount == planeCount)
			{
				return CD3DX12_BARRIER_SUBRESOURCE_RANGE(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
			}

			return CD3DX12_BARRIER_SUBRESOURCE_RANGE(range.m_BaseMip, range.m_MipCount,
				range.m_BaseArraySlice, range.m_ArraySliceCount, planeRange.m_BasePlane,
				planeRange.m_PlaneCount);
		}
	}

	D3D12_BARRIER_SYNC ToD3D12BarrierSync(RHIStage stages) noexcept
	{
		if (stages == RHIStage::None)
		{
			return D3D12_BARRIER_SYNC_NONE;
		}

		D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;
		if (Test(stages, RHIStage::DrawIndirect))
		{
			sync |= D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
		}
		if (Test(stages, RHIStage::IndexInput))
		{
			sync |= D3D12_BARRIER_SYNC_INDEX_INPUT;
		}
		if (Test(stages, RHIStage::VertexShader))
		{
			sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
		}
		if (Test(stages, RHIStage::PixelShader))
		{
			sync |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
		}
		if (Test(stages, RHIStage::ComputeShader))
		{
			sync |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
		}
		if (Test(stages, RHIStage::RenderTarget))
		{
			sync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
		}
		if (Test(stages, RHIStage::DepthStencil))
		{
			sync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
		}
		if (Test(stages, RHIStage::Copy))
		{
			sync |= D3D12_BARRIER_SYNC_COPY;
		}
		if (Test(stages, RHIStage::Resolve))
		{
			sync |= D3D12_BARRIER_SYNC_RESOLVE;
		}
		if (Test(stages, RHIStage::Present))
		{
			return D3D12_BARRIER_SYNC_ALL;
		}
		return sync;
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

	D3D12_TEXTURE_BARRIER BuildD3D12TextureBarrier(const RHITextureBarrier& barrier,
		ID3D12Resource* resource, const D3D12_RESOURCE_DESC& resourceDesc) noexcept
	{
		return CD3DX12_TEXTURE_BARRIER(ToD3D12BarrierSync(barrier.m_Before.m_Stages),
			ToD3D12BarrierSync(barrier.m_After.m_Stages),
			ToD3D12BarrierAccess(barrier.m_Before.m_Access),
			ToD3D12BarrierAccess(barrier.m_After.m_Access),
			ToD3D12BarrierLayout(barrier.m_Before.m_Layout),
			ToD3D12BarrierLayout(barrier.m_After.m_Layout), resource,
			BuildD3D12BarrierSubresourceRange(barrier.m_Subresources, resourceDesc));
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
			states |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
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
