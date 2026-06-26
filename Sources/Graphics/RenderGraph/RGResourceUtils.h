#pragma once
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/RHI/RHITexture.h"

namespace gglab
{
	constexpr inline RHIResourceState CommonRHIResourceState() noexcept
	{
		return
		{
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

	template<typename ResourceUsage>
	constexpr inline RHIResourceState ToRHIResourceState(ResourceUsage usage) noexcept = delete;

	template<>
	constexpr inline RHIResourceState ToRHIResourceState<RGTextureUsage>(RGTextureUsage usage) noexcept
	{
		using U = RGTextureUsage;

		GGLAB_ASSERT_MSG(
			usage == U::None ||
			usage == U::Sample ||
			usage == U::RenderTarget ||
			usage == U::DepthStencil ||
			usage == U::DepthStencilRead ||
			usage == U::UAV ||
			usage == U::CopySrc ||
			usage == U::CopyDst ||
			usage == U::Present,
			"RGTextureUsage must describe exactly one access type.");

		switch (usage)
		{
		case U::None:
			return CommonRHIResourceState();
		case U::Sample:
			return
			{
				.m_Access = RHIAccess::ShaderResource,
				.m_Layout = RHILayout::ShaderResource,
			};
		case U::RenderTarget:
			return
			{
				.m_Access = RHIAccess::RenderTarget,
				.m_Layout = RHILayout::RenderTarget,
			};
		case U::DepthStencil:
			return
			{
				.m_Access = RHIAccess::DepthStencilWrite,
				.m_Layout = RHILayout::DepthStencilWrite,
			};
		case U::DepthStencilRead:
			return
			{
				.m_Access = RHIAccess::DepthStencilRead,
				.m_Layout = RHILayout::DepthStencilRead,
			};
		case U::UAV:
			return
			{
				.m_Access = RHIAccess::UnorderedAccess,
				.m_Layout = RHILayout::UnorderedAccess,
			};
		case U::CopySrc:
			return
			{
				.m_Access = RHIAccess::CopySource,
				.m_Layout = RHILayout::CopySource,
			};
		case U::CopyDst:
			return
			{
				.m_Access = RHIAccess::CopyDest,
				.m_Layout = RHILayout::CopyDest,
			};
		case U::Present:
			return
			{
				.m_Access = RHIAccess::Present,
				.m_Layout = RHILayout::Present,
			};
		}

		GGLAB_UNREACHABLE("Unhandled RGTextureUsage.");
	}

	template<>
	constexpr inline RHIResourceState ToRHIResourceState<RGBufferUsage>(RGBufferUsage usage) noexcept
	{
		using U = RGBufferUsage;

		constexpr U readUsages = U::Vertex | U::Index | U::Constant;
		const bool hasReadUsage = Any(usage & readUsages);
		const bool hasExclusiveUsage = Test(usage, U::UAV | U::CopySrc | U::CopyDst);
		GGLAB_ASSERT_MSG(
			usage == U::None ||
			(hasReadUsage && !hasExclusiveUsage) ||
			usage == U::UAV ||
			usage == U::CopySrc ||
			usage == U::CopyDst,
			"RGBufferUsage contains incompatible access types.");

		if (usage == U::None)
		{
			return CommonRHIResourceState();
		}

		if (hasReadUsage)
		{
			RHIResourceState state =
			{
				.m_Access = RHIAccess::Common,
				.m_Layout = RHILayout::Common,
			};
			if (Test(usage, U::Vertex))
			{
				state.m_Access |= RHIAccess::VertexBuffer;
			}
			if (Test(usage, U::Index))
			{
				state.m_Access |= RHIAccess::IndexBuffer;
			}
			if (Test(usage, U::Constant))
			{
				state.m_Access |= RHIAccess::ConstantBuffer;
			}
			return state;
		}

		switch (usage)
		{
		case U::UAV:
			return
			{
				.m_Access = RHIAccess::UnorderedAccess,
				.m_Layout = RHILayout::Common,
			};
		case U::CopySrc:
			return
			{
				.m_Access = RHIAccess::CopySource,
				.m_Layout = RHILayout::Common,
			};
		case U::CopyDst:
			return
			{
				.m_Access = RHIAccess::CopyDest,
				.m_Layout = RHILayout::Common,
			};
		}

		GGLAB_UNREACHABLE("Unhandled RGBufferUsage.");
	}

	constexpr inline RHIResourceState ToRHIResourceState(uint64_t usageBits, RGResourceType resourceType) noexcept
	{
		switch (resourceType)
		{
		case RGResourceType::RGTexture:
			return ToRHIResourceState(static_cast<RGTextureUsage>(usageBits));
		case RGResourceType::RGBuffer:
			return ToRHIResourceState(static_cast<RGBufferUsage>(usageBits));
		}

		GGLAB_UNREACHABLE("Unhandled RGResourceType.");
	}

}
