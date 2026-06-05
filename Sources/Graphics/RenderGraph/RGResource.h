#pragma once
#include "Graphics/RenderGraph/RGResourceHandle.h"
#include "Core/EnumFlags.h"

namespace gglab
{
	enum RGResourceType : uint8_t
	{
		RGTexture,
		RGBuffer,
	};

	enum class RGResourceAccessType : uint8_t
	{
		Read,
		Write,
	};

	template<typename RESOURCE>
	struct RGResourceTraits;

	// TODO: Wrap DXGI_FORMAT& Resource States
	using RGResourceFormat = DXGI_FORMAT;
	//using RGResourceUsage = D3D12_RESOURCE_STATES;

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
		DepthStencilRead = 1u << 7,
	};
	GGLAB_ENUM_FLAGS(RGTextureUsage);

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

	struct RGTextureDesc
	{
		// TODO: 3d texture support
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint16_t m_ArraySize = 1;
		uint16_t m_MipLevels = 1;
		uint16_t m_SampleCount = 1;
		RGResourceFormat m_Format = DXGI_FORMAT_UNKNOWN;
		RGTextureUsage m_Usage = RGTextureUsage::None;

		bool operator==(const RGTextureDesc&) const noexcept = default;
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
		uint64_t m_SizeInBytes = 0;
		uint32_t m_Stride = 0;
		RGResourceFormat m_Format = DXGI_FORMAT_UNKNOWN;
		RGBufferUsage m_Usage = RGBufferUsage::None;

		bool operator==(const RGBufferDesc&) const noexcept = default;
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

	class DX12Texture;

	template<>
	struct RGResourceTraits<RGTextureResource>
	{
		using Usage = RGTextureUsage;
		using Native = DX12Texture;
		static constexpr RGResourceType ResourceType = RGResourceType::RGTexture;
	};

	class DX12Buffer;

	template<>
	struct RGResourceTraits<RGBufferResource>
	{
		using Usage = RGBufferUsage;
		using Native = DX12Buffer;
		static constexpr RGResourceType ResourceType = RGResourceType::RGBuffer;
	};
}
