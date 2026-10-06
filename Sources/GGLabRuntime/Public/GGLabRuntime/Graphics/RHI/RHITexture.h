#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"

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

	enum class RHITextureCreateFlags : uint8_t
	{
		None = 0,
		CubeCompatible = 1u << 0,
	};
	GGLAB_ENUM_FLAGS(RHITextureCreateFlags);

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

	struct RHITextureDesc
	{
		RHITextureDimension m_Dimension = RHITextureDimension::Texture2D;
		RHIFormat m_Format = RHIFormat::Unknown;
		RHITextureUsage m_Usage = RHITextureUsage::None;
		RHITextureCreateFlags m_CreateFlags = RHITextureCreateFlags::None;
		RHIExtent3D m_Extent{};
		uint16_t m_ArraySize = 1;
		uint16_t m_MipLevels = 1;
		uint16_t m_SampleCount = 1;
		const char* m_DebugName = nullptr;
		std::optional<RHIClearValue> m_ClearValue = std::nullopt;
	};

	struct RHIOwnedTextureCreateInfo
	{
		RHITextureDesc m_Desc{};
		RHIResourceState m_InitialState = UndefinedRHITextureState();
	};

	[[nodiscard]] constexpr inline RHITextureAspect GetRHITextureAspects(
		const RHITextureDesc& desc) noexcept
	{
		const RHIFormatInfo& formatInfo = GetRHIFormatInfo(desc.m_Format);
		if (Test(desc.m_Usage, RHITextureUsage::DepthStencil) &&
			formatInfo.m_DepthStencilAspects != RHITextureAspect::None)
		{
			return formatInfo.m_DepthStencilAspects;
		}
		return formatInfo.m_Aspects;
	}

	[[nodiscard]] constexpr inline uint32_t GetRHITexturePlaneCount(
		const RHITextureDesc& desc) noexcept
	{
		return GetRHIFormatInfo(desc.m_Format).m_PlaneCount;
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

		[[nodiscard]] bool IsValid() const noexcept { return !m_Subresources.empty(); }
	};

	struct RHITextureReadbackSubresource
	{
		uint64_t m_BufferOffset = 0;
		uint64_t m_RowPitch = 0;
		uint64_t m_RowSizeInBytes = 0;
		uint64_t m_SlicePitch = 0;
		uint32_t m_RowCount = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_Depth = 1;
		uint32_t m_MipLevel = 0;
		uint32_t m_ArraySlice = 0;
	};

	struct RHITextureReadbackRequest
	{
		RHIBufferOwner m_Buffer;
		uint64_t m_BufferSizeInBytes = 0;
		RHITextureDesc m_TextureDesc{};
		std::vector<RHITextureReadbackSubresource> m_Subresources;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return static_cast<bool>(m_Buffer) && m_BufferSizeInBytes > 0 &&
				!m_Subresources.empty();
		}
	};

	// Backend-neutral placed layout for copying one 2D color subresource into a
	// buffer. The alignments satisfy the D3D12 placed-footprint rules; Vulkan
	// accepts the same layout because the texel size must divide the row pitch.
	inline constexpr uint64_t RHITextureCopyRowPitchAlignment = 256;
	inline constexpr uint64_t RHITextureCopyPlacementAlignment = 512;

	struct RHITextureCopyFootprint
	{
		RHIFormat m_Format = RHIFormat::Unknown;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_BytesPerTexel = 0;
		uint64_t m_RowSizeInBytes = 0;
		uint64_t m_RowPitch = 0;
		uint64_t m_SizeInBytes = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return m_SizeInBytes != 0; }
	};

	// Returns an invalid footprint for an empty extent or for formats without a
	// single-plane, uncompressed color texel whose size divides the row pitch
	// alignment (block-compressed, depth/stencil, typeless and 96-bit formats).
	[[nodiscard]] constexpr RHITextureCopyFootprint ComputeRHITextureCopyFootprint(
		RHIFormat format, uint32_t width, uint32_t height) noexcept
	{
		const RHIFormatInfo& formatInfo = GetRHIFormatInfo(format);
		const uint32_t bytesPerTexel = formatInfo.m_BytesPerBlock;
		if (width == 0 || height == 0 || formatInfo.m_IsTypeless ||
			formatInfo.m_Aspects != RHITextureAspect::Color || formatInfo.m_PlaneCount != 1 ||
			formatInfo.m_BlockWidth != 1 || formatInfo.m_BlockHeight != 1 || bytesPerTexel == 0 ||
			RHITextureCopyRowPitchAlignment % bytesPerTexel != 0)
		{
			return {};
		}

		const uint64_t rowSizeInBytes = static_cast<uint64_t>(width) * bytesPerTexel;
		const uint64_t rowPitch = (rowSizeInBytes + RHITextureCopyRowPitchAlignment - 1) /
			RHITextureCopyRowPitchAlignment * RHITextureCopyRowPitchAlignment;
		return RHITextureCopyFootprint{
			.m_Format = format,
			.m_Width = width,
			.m_Height = height,
			.m_BytesPerTexel = bytesPerTexel,
			.m_RowSizeInBytes = rowSizeInBytes,
			.m_RowPitch = rowPitch,
			.m_SizeInBytes = rowPitch * height,
		};
	}

	struct RHITextureViewDesc
	{
		RHITextureViewType m_Type = RHITextureViewType::ShaderResource;
		RHITextureViewDimension m_Dimension = RHITextureViewDimension::Unknown;
		RHIFormat m_Format = RHIFormat::Unknown;

		RHISubresourceRange m_Subresources{};
		float m_ResourceMinLODClamp = 0.0f;
		uint8_t m_ReadOnlyDepth = 0;
		uint8_t m_ReadOnlyStencil = 0;

		bool operator==(const RHITextureViewDesc&) const noexcept = default;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_Type, m_Dimension, m_Format, m_Subresources.m_BaseMip,
				m_Subresources.m_MipCount, m_Subresources.m_BaseArraySlice,
				m_Subresources.m_ArraySliceCount, m_Subresources.m_Aspects, m_ResourceMinLODClamp,
				m_ReadOnlyDepth, m_ReadOnlyStencil);
		}
	};

	[[nodiscard]] constexpr inline RHIPortabilityValidationResult
		ValidateRHITextureViewPortability(const RHITextureViewDesc& desc,
			const RHIPortabilityCapabilities& capabilities) noexcept
	{
		if (desc.m_ResourceMinLODClamp != 0.0f && !capabilities.m_ImageViewMinLod)
		{
			return { .m_Error = RHIPortabilityValidationError::ImageViewMinLodUnsupported };
		}
		return {};
	}
}
