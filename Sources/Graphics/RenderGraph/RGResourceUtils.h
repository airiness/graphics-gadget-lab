#pragma once
#include "Graphics/RenderGraph/RGResource.h"

namespace gglab
{
	constexpr inline RGBarrierState CommonRGBarrierState() noexcept
	{
		return
		{
			.m_Stage = RGStage::All,
			.m_Access = RGAccess::Common,
			.m_Layout = RGLayout::Common,
		};
	}

	constexpr inline bool HasUavAccess(const RGBarrierState& state) noexcept
	{
		return Test(state.m_Access, RGAccess::UnorderedAccess);
	}

	constexpr inline bool NeedsRGBarrier(const RGBarrierState& before, const RGBarrierState& after) noexcept
	{
		return before != after || (HasUavAccess(before) && HasUavAccess(after));
	}

	template<typename ResourceUsage>
	constexpr inline RGBarrierState ToRGBarrierState(ResourceUsage usage) noexcept = delete;

	template<>
	constexpr inline RGBarrierState ToRGBarrierState<RGTextureUsage>(RGTextureUsage usage) noexcept
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
			return CommonRGBarrierState();
		case U::Sample:
			return
			{
				.m_Stage = RGStage::AllShading,
				.m_Access = RGAccess::ShaderResource,
				.m_Layout = RGLayout::ShaderResource,
			};
		case U::RenderTarget:
			return
			{
				.m_Stage = RGStage::RenderTarget,
				.m_Access = RGAccess::RenderTarget,
				.m_Layout = RGLayout::RenderTarget,
			};
		case U::DepthStencil:
			return
			{
				.m_Stage = RGStage::DepthStencil,
				.m_Access = RGAccess::DepthStencilWrite,
				.m_Layout = RGLayout::DepthStencilWrite,
			};
		case U::DepthStencilRead:
			return
			{
				.m_Stage = RGStage::DepthStencil,
				.m_Access = RGAccess::DepthStencilRead,
				.m_Layout = RGLayout::DepthStencilRead,
			};
		case U::UAV:
			return
			{
				.m_Stage = RGStage::AllShading,
				.m_Access = RGAccess::UnorderedAccess,
				.m_Layout = RGLayout::UnorderedAccess,
			};
		case U::CopySrc:
			return
			{
				.m_Stage = RGStage::Copy,
				.m_Access = RGAccess::CopySource,
				.m_Layout = RGLayout::CopySource,
			};
		case U::CopyDst:
			return
			{
				.m_Stage = RGStage::Copy,
				.m_Access = RGAccess::CopyDest,
				.m_Layout = RGLayout::CopyDest,
			};
		case U::Present:
			return
			{
				.m_Stage = RGStage::All,
				.m_Access = RGAccess::Common,
				.m_Layout = RGLayout::Present,
			};
		}

		GGLAB_UNREACHABLE("Unhandled RGTextureUsage.");
	}

	template<>
	constexpr inline RGBarrierState ToRGBarrierState<RGBufferUsage>(RGBufferUsage usage) noexcept
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
			return CommonRGBarrierState();
		}

		if (hasReadUsage)
		{
			RGBarrierState state =
			{
				.m_Stage = RGStage::None,
				.m_Access = RGAccess::Common,
				.m_Layout = RGLayout::Common,
			};
			if (Test(usage, U::Vertex))
			{
				state.m_Stage |= RGStage::VertexShading;
				state.m_Access |= RGAccess::VertexBuffer;
			}
			if (Test(usage, U::Index))
			{
				state.m_Stage |= RGStage::IndexInput;
				state.m_Access |= RGAccess::IndexBuffer;
			}
			if (Test(usage, U::Constant))
			{
				state.m_Stage |= RGStage::AllShading;
				state.m_Access |= RGAccess::ConstantBuffer;
			}
			return state;
		}

		switch (usage)
		{
		case U::UAV:
			return
			{
				.m_Stage = RGStage::AllShading,
				.m_Access = RGAccess::UnorderedAccess,
				.m_Layout = RGLayout::Common,
			};
		case U::CopySrc:
			return
			{
				.m_Stage = RGStage::Copy,
				.m_Access = RGAccess::CopySource,
				.m_Layout = RGLayout::Common,
			};
		case U::CopyDst:
			return
			{
				.m_Stage = RGStage::Copy,
				.m_Access = RGAccess::CopyDest,
				.m_Layout = RGLayout::Common,
			};
		}

		GGLAB_UNREACHABLE("Unhandled RGBufferUsage.");
	}

	constexpr inline RGBarrierState ToRGBarrierState(uint64_t usageBits, RGResourceType resourceType) noexcept
	{
		switch (resourceType)
		{
		case RGResourceType::RGTexture:
			return ToRGBarrierState(static_cast<RGTextureUsage>(usageBits));
		case RGResourceType::RGBuffer:
			return ToRGBarrierState(static_cast<RGBufferUsage>(usageBits));
		}

		GGLAB_UNREACHABLE("Unhandled RGResourceType.");
	}
}
