#pragma once
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/RHI/RHITexture.h"

namespace gglab
{
	constexpr inline RHIResourceState CommonRHIResourceState() noexcept
	{
		return
		{
			.m_Stages = RHIStage::None,
			.m_Access = RHIAccess::Common,
			.m_Layout = RHILayout::Common,
		};
	}

	constexpr inline bool HasUavAccess(const RHIResourceState& state) noexcept
	{
		return Test(state.m_Access, RHIAccess::UnorderedAccess);
	}

	constexpr inline bool NeedsRHIResourceBarrier(const RHIResourceState& before, const RHIResourceState& after) noexcept
	{
		return before != after || (HasUavAccess(before) && HasUavAccess(after));
	}

	constexpr inline RHITextureUsage ToRHIUsage(RGTextureAccess access) noexcept
	{
		switch (access)
		{
		case RGTextureAccess::None: return RHITextureUsage::None;
		case RGTextureAccess::Sample: return RHITextureUsage::Sampled;
		case RGTextureAccess::RenderTarget: return RHITextureUsage::RenderTarget;
		case RGTextureAccess::DepthStencilWrite:
		case RGTextureAccess::DepthStencilRead: return RHITextureUsage::DepthStencil;
		case RGTextureAccess::UnorderedAccess: return RHITextureUsage::UnorderedAccess;
		case RGTextureAccess::CopySource: return RHITextureUsage::CopySource;
		case RGTextureAccess::CopyDest: return RHITextureUsage::CopyDest;
		case RGTextureAccess::Present: return RHITextureUsage::Present;
		}
		GGLAB_UNREACHABLE("Unhandled RGTextureAccess.");
	}

	constexpr inline RHIBufferUsage ToRHIUsage(RGBufferAccess access) noexcept
	{
		switch (access)
		{
		case RGBufferAccess::None: return RHIBufferUsage::None;
		case RGBufferAccess::Vertex: return RHIBufferUsage::Vertex;
		case RGBufferAccess::Index: return RHIBufferUsage::Index;
		case RGBufferAccess::Constant: return RHIBufferUsage::Constant;
		case RGBufferAccess::StructuredRead: return RHIBufferUsage::Structured;
		case RGBufferAccess::UnorderedAccess: return RHIBufferUsage::UnorderedAccess;
		case RGBufferAccess::CopySource: return RHIBufferUsage::CopySource;
		case RGBufferAccess::CopyDest: return RHIBufferUsage::CopyDest;
		case RGBufferAccess::IndirectArgument: return RHIBufferUsage::IndirectArgument;
		}
		GGLAB_UNREACHABLE("Unhandled RGBufferAccess.");
	}

	constexpr inline RHIResourceState ToRHIResourceState(RGTextureAccess access) noexcept
	{
		switch (access)
		{
		case RGTextureAccess::None:
			return CommonRHIResourceState();
		case RGTextureAccess::Sample:
			return
			{
				.m_Stages = RHIStage::PixelShader | RHIStage::ComputeShader,
				.m_Access = RHIAccess::ShaderResource,
				.m_Layout = RHILayout::ShaderResource,
			};
		case RGTextureAccess::RenderTarget:
			return
			{
				.m_Stages = RHIStage::RenderTarget,
				.m_Access = RHIAccess::RenderTarget,
				.m_Layout = RHILayout::RenderTarget,
			};
		case RGTextureAccess::DepthStencilWrite:
			return
			{
				.m_Stages = RHIStage::DepthStencil,
				.m_Access = RHIAccess::DepthStencilWrite,
				.m_Layout = RHILayout::DepthStencilWrite,
			};
		case RGTextureAccess::DepthStencilRead:
			return
			{
				.m_Stages = RHIStage::DepthStencil,
				.m_Access = RHIAccess::DepthStencilRead,
				.m_Layout = RHILayout::DepthStencilRead,
			};
		case RGTextureAccess::UnorderedAccess:
			return
			{
				.m_Stages = RHIStage::ComputeShader,
				.m_Access = RHIAccess::UnorderedAccess,
				.m_Layout = RHILayout::UnorderedAccess,
			};
		case RGTextureAccess::CopySource:
			return
			{
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopySource,
				.m_Layout = RHILayout::CopySource,
			};
		case RGTextureAccess::CopyDest:
			return
			{
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopyDest,
				.m_Layout = RHILayout::CopyDest,
			};
		case RGTextureAccess::Present:
			return
			{
				.m_Stages = RHIStage::Present,
				.m_Access = RHIAccess::Present,
				.m_Layout = RHILayout::Present,
			};
		}

		GGLAB_UNREACHABLE("Unhandled RGTextureAccess.");
	}

	constexpr inline RHIResourceState ToRHIResourceState(RGBufferAccess access) noexcept
	{
		switch (access)
		{
		case RGBufferAccess::None:
			return CommonRHIResourceState();
		case RGBufferAccess::Vertex:
			return { .m_Stages = RHIStage::VertexInput, .m_Access = RHIAccess::VertexBuffer, .m_Layout = RHILayout::Common };
		case RGBufferAccess::Index:
			return { .m_Stages = RHIStage::VertexInput, .m_Access = RHIAccess::IndexBuffer, .m_Layout = RHILayout::Common };
		case RGBufferAccess::Constant:
			return { .m_Stages = RHIStage::VertexShader | RHIStage::PixelShader | RHIStage::ComputeShader, .m_Access = RHIAccess::ConstantBuffer, .m_Layout = RHILayout::Common };
		case RGBufferAccess::StructuredRead:
			return { .m_Stages = RHIStage::VertexShader | RHIStage::PixelShader | RHIStage::ComputeShader, .m_Access = RHIAccess::ShaderResource, .m_Layout = RHILayout::Common };
		case RGBufferAccess::UnorderedAccess:
			return
			{
				.m_Stages = RHIStage::ComputeShader,
				.m_Access = RHIAccess::UnorderedAccess,
				.m_Layout = RHILayout::Common,
			};
		case RGBufferAccess::CopySource:
			return
			{
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopySource,
				.m_Layout = RHILayout::Common,
			};
		case RGBufferAccess::CopyDest:
			return
			{
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopyDest,
				.m_Layout = RHILayout::Common,
			};
		case RGBufferAccess::IndirectArgument:
			return { .m_Stages = RHIStage::DrawIndirect, .m_Access = RHIAccess::IndirectArgument, .m_Layout = RHILayout::Common };
		}

		GGLAB_UNREACHABLE("Unhandled RGBufferAccess.");
	}

	constexpr inline RHIResourceState ToRHIResourceState(uint64_t accessValue, RGResourceType resourceType) noexcept
	{
		switch (resourceType)
		{
		case RGResourceType::RGTexture:
			return ToRHIResourceState(static_cast<RGTextureAccess>(accessValue));
		case RGResourceType::RGBuffer:
			return ToRHIResourceState(static_cast<RGBufferAccess>(accessValue));
		}

		GGLAB_UNREACHABLE("Unhandled RGResourceType.");
	}

}
