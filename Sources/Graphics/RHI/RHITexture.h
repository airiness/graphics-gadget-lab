#pragma once
#include "Graphics/RHI/RHIResource.h"

#include <cstdint>
#include <tuple>
#include <vector>

namespace gglab
{
	enum class RHITextureUsage : uint32_t
	{
		None = 0,
		Sampled = 1u << 0,
		RenderTarget = 1u << 1,
		DepthStencil = 1u << 2,
		UnorderedAccess = 1u << 3,
		CopySource = 1u << 4,
		CopyDest = 1u << 5,
		Present = 1u << 6,
	};
	GGLAB_ENUM_FLAGS(RHITextureUsage);

	enum class RHITextureDimension : uint8_t
	{
		Texture1D,
		Texture2D,
		Texture3D,
	};

	enum class RHITextureViewType : uint8_t
	{
		RenderTarget,
		DepthStencil,
		ShaderResource,
		UnorderedAccess,
	};

	enum class RHITextureViewDimension : uint8_t
	{
		Unknown,
		Texture1D,
		Texture1DArray,
		Texture2D,
		Texture2DArray,
		Texture3D,
		TextureCube,
		TextureCubeArray,
	};

	[[nodiscard]] constexpr inline RHITextureAspect GetRHIFormatAspects(RHIFormat format) noexcept
	{
		switch (format)
		{
		case RHIFormat::D24UnormS8Uint:
			return RHITextureAspect::DepthStencil;
		case RHIFormat::D32Float:
			return RHITextureAspect::Depth;
		default:
			return RHITextureAspect::Color;
		}
	}

	struct RHITextureDesc
	{
		RHITextureDimension m_Dimension = RHITextureDimension::Texture2D;
		RHIFormat m_Format = RHIFormat::Unknown;
		RHITextureUsage m_Usage = RHITextureUsage::None;
		RHIExtent3D m_Extent{};
		uint16_t m_ArraySize = 1;
		uint16_t m_MipLevels = 1;
		uint16_t m_SampleCount = 1;
		const char* m_DebugName = nullptr;
		std::optional<RHIClearValue> m_ClearValue = std::nullopt;
	};

	[[nodiscard]] constexpr inline RHITextureAspect GetRHITextureAspects(const RHITextureDesc& desc) noexcept
	{
		if (desc.m_Format == RHIFormat::R32Typeless && Test(desc.m_Usage, RHITextureUsage::DepthStencil))
		{
			return RHITextureAspect::Depth;
		}
		return GetRHIFormatAspects(desc.m_Format);
	}

	struct RHIImportedTextureDesc
	{
		RHITextureDesc m_Desc;
		RHIExternalResourceDesc m_External;
	};

	// Non-owning upload data.
	// m_Data pointers must remain valid until
	// RHITransferContext::UploadTexture records the upload commands.
	struct RHITextureSubresourceData
	{
		const void* m_Data = nullptr;
		uint64_t m_RowPitch = 0;
		uint64_t m_SlicePitch = 0;
	};

	struct RHITextureUploadData
	{
		std::vector<RHITextureSubresourceData> m_Subresources;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return !m_Subresources.empty();
		}
	};

	struct RHITextureViewDesc
	{
		RHITextureViewType m_Type = RHITextureViewType::ShaderResource;
		RHITextureViewDimension m_Dimension = RHITextureViewDimension::Unknown;
		RHIFormat m_Format = RHIFormat::Unknown;

		RHISubresourceRange m_Subresources{};
		float m_ResourceMinLODClamp = 0.0f;

		bool operator==(const RHITextureViewDesc&) const noexcept = default;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(
				m_Type,
				m_Dimension,
				m_Format,
				m_Subresources.m_BaseMip,
				m_Subresources.m_MipCount,
				m_Subresources.m_BaseArraySlice,
				m_Subresources.m_ArraySliceCount,
				m_Subresources.m_Aspects,
				m_ResourceMinLODClamp);
		}
	};
}
