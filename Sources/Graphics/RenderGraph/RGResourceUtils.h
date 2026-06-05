#pragma once
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/DX12/DX12Texture.h"

namespace gglab
{
	struct RGBarrierState
	{
		D3D12_BARRIER_SYNC m_Sync = D3D12_BARRIER_SYNC_ALL;
		D3D12_BARRIER_ACCESS m_Access = D3D12_BARRIER_ACCESS_COMMON;
		D3D12_BARRIER_LAYOUT m_Layout = D3D12_BARRIER_LAYOUT_COMMON;

		bool operator==(const RGBarrierState&) const noexcept = default;
	};

	constexpr inline RGBarrierState CommonRGBarrierState() noexcept
	{
		return {};
	}

	constexpr inline bool HasUavAccess(const RGBarrierState& state) noexcept
	{
		return (state.m_Access & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS) != 0;
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
				.m_Sync = D3D12_BARRIER_SYNC_ALL_SHADING,
				.m_Access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
				.m_Layout = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
			};
		case U::RenderTarget:
			return
			{
				.m_Sync = D3D12_BARRIER_SYNC_RENDER_TARGET,
				.m_Access = D3D12_BARRIER_ACCESS_RENDER_TARGET,
				.m_Layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
			};
		case U::DepthStencil:
			return
			{
				.m_Sync = D3D12_BARRIER_SYNC_DEPTH_STENCIL,
				.m_Access = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE,
				.m_Layout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
			};
		case U::DepthStencilRead:
			return
			{
				.m_Sync = D3D12_BARRIER_SYNC_DEPTH_STENCIL,
				.m_Access = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ,
				.m_Layout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ,
			};
		case U::UAV:
			return
			{
				.m_Sync = D3D12_BARRIER_SYNC_ALL_SHADING,
				.m_Access = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
				.m_Layout = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,
			};
		case U::CopySrc:
			return
			{
				.m_Sync = D3D12_BARRIER_SYNC_COPY,
				.m_Access = D3D12_BARRIER_ACCESS_COPY_SOURCE,
				.m_Layout = D3D12_BARRIER_LAYOUT_COPY_SOURCE,
			};
		case U::CopyDst:
			return
			{
				.m_Sync = D3D12_BARRIER_SYNC_COPY,
				.m_Access = D3D12_BARRIER_ACCESS_COPY_DEST,
				.m_Layout = D3D12_BARRIER_LAYOUT_COPY_DEST,
			};
		case U::Present:
			return
			{
				.m_Sync = D3D12_BARRIER_SYNC_ALL,
				.m_Access = D3D12_BARRIER_ACCESS_COMMON,
				.m_Layout = D3D12_BARRIER_LAYOUT_PRESENT,
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
				.m_Sync = D3D12_BARRIER_SYNC_NONE,
				.m_Access = D3D12_BARRIER_ACCESS_COMMON,
			};
			if (Test(usage, U::Vertex))
			{
				state.m_Sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
				state.m_Access |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
			}
			if (Test(usage, U::Index))
			{
				state.m_Sync |= D3D12_BARRIER_SYNC_INDEX_INPUT;
				state.m_Access |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
			}
			if (Test(usage, U::Constant))
			{
				state.m_Sync |= D3D12_BARRIER_SYNC_ALL_SHADING;
				state.m_Access |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
			}
			return state;
		}

		switch (usage)
		{
		case U::UAV:
			return
			{
				.m_Sync = D3D12_BARRIER_SYNC_ALL_SHADING,
				.m_Access = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
			};
		case U::CopySrc:
			return
			{
				.m_Sync = D3D12_BARRIER_SYNC_COPY,
				.m_Access = D3D12_BARRIER_ACCESS_COPY_SOURCE,
			};
		case U::CopyDst:
			return
			{
				.m_Sync = D3D12_BARRIER_SYNC_COPY,
				.m_Access = D3D12_BARRIER_ACCESS_COPY_DEST,
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
