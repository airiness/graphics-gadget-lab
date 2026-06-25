#pragma once
#include "Core/EnumFlags.h"
#include "Graphics/RHI/RHITypes.h"

#include <cstdint>
#include <limits>

namespace gglab
{
	enum class RGStage : uint64_t
	{
		None = 0,
		All = 1ull << 0,
		AllShading = 1ull << 1,
		VertexShading = 1ull << 2,
		IndexInput = 1ull << 3,
		RenderTarget = 1ull << 4,
		DepthStencil = 1ull << 5,
		Copy = 1ull << 6,
	};
	GGLAB_ENUM_FLAGS(RGStage);

	enum class RGAccess : uint64_t
	{
		None = 0,
		Common = 1ull << 0,
		ShaderResource = 1ull << 1,
		RenderTarget = 1ull << 2,
		DepthStencilRead = 1ull << 3,
		DepthStencilWrite = 1ull << 4,
		UnorderedAccess = 1ull << 5,
		CopySource = 1ull << 6,
		CopyDest = 1ull << 7,
		VertexBuffer = 1ull << 8,
		IndexBuffer = 1ull << 9,
		ConstantBuffer = 1ull << 10,
	};
	GGLAB_ENUM_FLAGS(RGAccess);

	enum class RGLayout : uint8_t
	{
		Common,
		ShaderResource,
		RenderTarget,
		DepthStencilRead,
		DepthStencilWrite,
		UnorderedAccess,
		CopySource,
		CopyDest,
		Present,
	};

	enum class RGTextureViewType : uint8_t
	{
		RTV,
		DSV,
		SRV,
		UAV,
	};

	enum class RGTextureViewDimension : uint8_t
	{
		Unknown,
		Texture2D,
		Texture2DArray,
		TextureCube,
		TextureCubeArray,
	};

	struct RGTextureViewDesc
	{
		static constexpr uint32_t AllMipLevels = std::numeric_limits<uint32_t>::max();

		RHIFormat m_Format = RHIFormat::Unknown;
		RGTextureViewDimension m_Dimension = RGTextureViewDimension::Unknown;

		uint32_t m_MostDetailedMip = 0;
		uint32_t m_MipLevels = AllMipLevels;
		uint32_t m_MipSlice = 0;
		uint32_t m_FirstArraySlice = 0;
		uint32_t m_ArraySize = 1;
		uint32_t m_First2DArrayFace = 0;
		uint32_t m_NumCubes = 1;
		uint32_t m_PlaneSlice = 0;
		float m_ResourceMinLODClamp = 0.0f;
	};

	struct RGBarrierState
	{
		RGStage m_Stage = RGStage::All;
		RGAccess m_Access = RGAccess::Common;
		RGLayout m_Layout = RGLayout::Common;

		bool operator==(const RGBarrierState&) const noexcept = default;
	};

	[[nodiscard]] constexpr inline RGTextureViewDesc MakeRGTexture2DArrayViewDesc(
		RHIFormat format,
		uint32_t mip,
		uint32_t firstSlice,
		uint32_t arraySize,
		uint32_t planeSlice = 0) noexcept
	{
		return RGTextureViewDesc{
			.m_Format = format,
			.m_Dimension = RGTextureViewDimension::Texture2DArray,
			.m_MipSlice = mip,
			.m_FirstArraySlice = firstSlice,
			.m_ArraySize = arraySize,
			.m_PlaneSlice = planeSlice,
		};
	}

	[[nodiscard]] constexpr inline RGTextureViewDesc MakeRGTexture2DViewDesc(
		RHIFormat format,
		uint32_t mip = 0,
		uint32_t mipLevels = 1,
		uint32_t planeSlice = 0) noexcept
	{
		return RGTextureViewDesc{
			.m_Format = format,
			.m_Dimension = RGTextureViewDimension::Texture2D,
			.m_MostDetailedMip = mip,
			.m_MipLevels = mipLevels,
			.m_MipSlice = mip,
			.m_PlaneSlice = planeSlice,
		};
	}
}
