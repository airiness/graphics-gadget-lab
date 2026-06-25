#pragma once
#include "Graphics/RHI/RHIResource.h"

#include <cstdint>
#include <limits>
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

	struct RHIImportedTextureDesc
	{
		RHITextureDesc m_Desc;
		RHIExternalResourceDesc m_External;
	};

	// Non-owning upload data.
	// m_Data pointers must remain valid until
	// RHITransferContext::UploadTexture has recorded the upload commands.
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
		static constexpr uint32_t AllMipLevels = std::numeric_limits<uint32_t>::max();

		RHITextureViewType m_Type = RHITextureViewType::ShaderResource;
		RHITextureViewDimension m_Dimension = RHITextureViewDimension::Unknown;
		RHIFormat m_Format = RHIFormat::Unknown;

		uint32_t m_MostDetailedMip = 0;
		uint32_t m_MipLevels = AllMipLevels;
		uint32_t m_MipSlice = 0;
		uint32_t m_FirstArraySlice = 0;
		uint32_t m_ArraySize = 1;
		uint32_t m_First2DArrayFace = 0;
		uint32_t m_NumCubes = 1;
		uint32_t m_PlaneSlice = 0;
		float m_ResourceMinLODClamp = 0.0f;

		bool operator==(const RHITextureViewDesc&) const noexcept = default;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(
				m_Type,
				m_Dimension,
				m_Format,
				m_MostDetailedMip,
				m_MipLevels,
				m_MipSlice,
				m_FirstArraySlice,
				m_ArraySize,
				m_First2DArrayFace,
				m_NumCubes,
				m_PlaneSlice,
				m_ResourceMinLODClamp);
		}
	};
}
