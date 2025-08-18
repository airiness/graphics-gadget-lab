#pragma once
#include "RGResourceHandle.h"
#include "EnumFlags.h"

namespace graphicsGadgetLab
{
	enum RGResourceType : uint8_t
	{
		RGTexture,
		RGBuffer,
	};

	template<typename RESOURCE>
	struct RGResourceTraits;

	// TODO: Wrap DXGI_FORMAT& Resource States
	using RGResourceFormat = DXGI_FORMAT;
	//using RGResourceUsage = D3D12_RESOURCE_STATES;

	template<typename ResourceUsage>
	inline D3D12_RESOURCE_STATES ToD3D12ResourceStates(ResourceUsage usage, bool unuse = false) noexcept = delete;

	enum class RGTextureUsage : uint32_t
	{
		None = 0,
		Sample = 1u << 0,		// SRV
		RenderTarget = 1u << 1,	// RTV
		DepthStencil = 1u << 2,	// DSV
		UAV = 1u << 3,			// UAV
		CopySrc = 1u << 4,
		CopyDst = 1u << 5,
		Present = 1u << 6,		// BackBuffer
	};
	GGLAB_ENUM_FLAGS(RGTextureUsage);

	template<>
	inline D3D12_RESOURCE_STATES ToD3D12ResourceStates<RGTextureUsage>(RGTextureUsage usage, bool depthReadOnly) noexcept
	{
		using U = RGTextureUsage;
		D3D12_RESOURCE_STATES states = D3D12_RESOURCE_STATE_COMMON;

		if (Test(usage, U::Sample))
		{
			states |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		}
		if (Test(usage, U::RenderTarget))
		{
			states |= D3D12_RESOURCE_STATE_RENDER_TARGET;
		}
		if (Test(usage, U::DepthStencil))
		{
			states |= depthReadOnly ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE;
		}
		if (Test(usage, U::UAV))
		{
			states |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}
		if (Test(usage, U::CopySrc))
		{
			states |= D3D12_RESOURCE_STATE_COPY_SOURCE;
		}
		if (Test(usage, U::CopyDst))
		{
			states |= D3D12_RESOURCE_STATE_COPY_DEST;
		}
		if (Test(usage, U::Present))
		{
			states |= D3D12_RESOURCE_STATE_PRESENT;
		}

		return states;
	}

	enum class RGBufferUsage : uint32_t
	{
		None = 0,
		Vertex = 1u << 0,
		Index = 1u << 1,
		Constant = 1u << 2,
		UAV = 1u << 3,
		CopySrc = 1u << 4,
		CopyDst = 1u << 5,
	};
	GGLAB_ENUM_FLAGS(RGBufferUsage);

	template<>
	inline D3D12_RESOURCE_STATES ToD3D12ResourceStates<RGBufferUsage>(RGBufferUsage usage, bool) noexcept
	{
		using U = RGBufferUsage;
		D3D12_RESOURCE_STATES states = D3D12_RESOURCE_STATE_COMMON;

		if (Test(usage, U::Vertex | U::Constant))
		{
			states |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		}
		if (Test(usage, U::Index))
		{
			states |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
		}
		if (Test(usage, U::UAV))
		{
			states |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}
		if (Test(usage, U::CopySrc))
		{
			states |= D3D12_RESOURCE_STATE_COPY_SOURCE;
		}
		if (Test(usage, U::CopyDst))
		{
			states |= D3D12_RESOURCE_STATE_COPY_DEST;
		}

		return states;
	}

	struct RGTextureDesc
	{
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint16_t m_ArraySize = 1;
		uint16_t m_MipLevels = 1;
		uint16_t m_SampleCount = 1;
		RGResourceFormat m_Format = DXGI_FORMAT_UNKNOWN;
		RGTextureUsage m_Usage = RGTextureUsage::None;
	};

	struct RGTextureSubresourceDesc
	{
		uint32_t m_BaseMip = 0;
		uint32_t m_LevelCount = 1;
		uint32_t m_BaseArray = 0;
		uint32_t m_ArrayCount = 1;
	};

	struct RGBufferDesc
	{
		uint64_t m_Size = 0;
		uint32_t m_Stride = 0;
		RGResourceFormat m_Format = DXGI_FORMAT_UNKNOWN;
		RGBufferUsage m_Usage = RGBufferUsage::None;
	};

	struct RGTextureResource
	{
		using Descriptor = RGTextureDesc;
		using SubresourceDescriptor = RGTextureSubresourceDesc;
		using Usage = RGTextureUsage;

		static constexpr RGTextureUsage DefaultReadUsage = RGTextureUsage::Sample;
		static constexpr RGTextureUsage DefaultWriteUsage = RGTextureUsage::RenderTarget;
		static constexpr RGTextureUsage DefaultNoneUsage = RGTextureUsage::None;
	};
	using RGTextureId = RGResourceId<RGTextureResource>;

	struct RGBufferResource
	{
		using Descriptor = RGBufferDesc;
		using SubresourceDescriptor = std::monostate;
		using Usage = RGBufferUsage;

		static constexpr RGBufferUsage DefaultReadUsage = RGBufferUsage::Vertex;
		static constexpr RGBufferUsage DefaultWriteUsage = RGBufferUsage::UAV;
		static constexpr RGBufferUsage DefaultNoneUsage = RGBufferUsage::None;
	};
	using RGBufferId = RGResourceId<RGBufferResource>;

	template<>
	struct RGResourceTraits<RGTextureResource>
	{
		using Usage = RGTextureUsage;
		using Bits = std::underlying_type_t<Usage>;
		static constexpr RGResourceType ResourceType = RGResourceType::RGTexture;
		static D3D12_RESOURCE_STATES ToState(Bits bits, bool depthReadOnly = false) noexcept
		{
			return ToD3D12ResourceStates<Usage>(static_cast<Usage>(bits), depthReadOnly);
		}
		static_assert(std::is_unsigned_v<Bits>, "Usage underlying type should be unsigned");
	};

	template<>
	struct RGResourceTraits<RGBufferResource>
	{
		using Usage = RGBufferUsage;
		using Bits = std::underlying_type_t<Usage>;
		static constexpr RGResourceType ResourceType = RGResourceType::RGBuffer;
		static D3D12_RESOURCE_STATES ToState(Bits bits, bool depthReadOnly = false) noexcept
		{
			return ToD3D12ResourceStates<Usage>(static_cast<Usage>(bits), depthReadOnly);
		}
		static_assert(std::is_unsigned_v<Bits>, "Usage underlying type should be unsigned");
	};
}