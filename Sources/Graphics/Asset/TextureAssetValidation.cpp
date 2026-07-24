#include "Core/Precompiled.h"
#include "Graphics/Asset/TextureAssetValidation.h"
#include "Graphics/RHI/RHIDevice.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] constexpr TextureStructureValidationResult Error(
			TextureStructureValidationError error) noexcept
		{
			return { .m_Error = error };
		}

		[[nodiscard]] constexpr bool IsTextureAssetViewDimension(
			RHITextureViewDimension dimension) noexcept
		{
			return dimension == RHITextureViewDimension::Texture2D ||
				dimension == RHITextureViewDimension::Texture2DArray ||
				dimension == RHITextureViewDimension::TextureCube ||
				dimension == RHITextureViewDimension::TextureCubeArray;
		}

		[[nodiscard]] TextureStructureValidationResult MapRHIValidationError(
			RHITextureValidationError error) noexcept
		{
			switch (error)
			{
			case RHITextureValidationError::None:
				return {};
			case RHITextureValidationError::InvalidFormat:
				return Error(TextureStructureValidationError::InvalidEnum);
			case RHITextureValidationError::InvalidExtent:
			case RHITextureValidationError::InvalidDimension:
				return Error(TextureStructureValidationError::InvalidExtent);
			case RHITextureValidationError::InvalidArraySize:
			case RHITextureValidationError::IncompatibleViewDimension:
			case RHITextureValidationError::InvalidSubresourceRange:
				return Error(TextureStructureValidationError::InvalidArrayConfiguration);
			case RHITextureValidationError::InvalidMipLevelCount:
				return Error(TextureStructureValidationError::InvalidMipCount);
			case RHITextureValidationError::IncompatibleViewFormat:
				return Error(TextureStructureValidationError::IncompatibleFormats);
			case RHITextureValidationError::InvalidUploadSubresourceCount:
				return Error(TextureStructureValidationError::InvalidSubresourceCount);
			case RHITextureValidationError::InvalidUploadSubresourceData:
				break;
			case RHITextureValidationError::InvalidUploadRowPitch:
				return Error(TextureStructureValidationError::InvalidRowPitch);
			case RHITextureValidationError::InvalidUploadSlicePitch:
				return Error(TextureStructureValidationError::InvalidSlicePitch);
			case RHITextureValidationError::InvalidUsage:
			case RHITextureValidationError::InvalidSampleCount:
			case RHITextureValidationError::InvalidClearValue:
			case RHITextureValidationError::UnsupportedUploadFormat:
				return Error(TextureStructureValidationError::InvalidEnum);
			}
			return Error(TextureStructureValidationError::InvalidDataSize);
		}

		[[nodiscard]] RHITextureUploadData BuildTextureUploadDataUnchecked(
			const TextureAssetData& data) noexcept
		{
			RHITextureUploadData uploadData{};
			uploadData.m_Subresources.resize(data.m_Subresources.size());
			for (const TextureAssetSubresource& subresource : data.m_Subresources)
			{
				const size_t index = static_cast<size_t>(subresource.m_ArraySlice) *
					data.m_MipLevels + subresource.m_MipLevel;
				uploadData.m_Subresources[index] =
				{
					.m_Data = data.m_Pixels.data() + subresource.m_DataOffset,
					.m_RowPitch = subresource.m_RowPitch,
					.m_SlicePitch = subresource.m_SlicePitch,
				};
			}
			return uploadData;
		}
	}

	std::string_view TextureStructureValidationErrorText(
		TextureStructureValidationError error) noexcept
	{
		switch (error)
		{
		case TextureStructureValidationError::None: return "none";
		case TextureStructureValidationError::InvalidEnum: return "invalid texture enum";
		case TextureStructureValidationError::IncompatibleFormats: return "incompatible texture formats";
		case TextureStructureValidationError::InvalidExtent: return "invalid texture extent";
		case TextureStructureValidationError::InvalidMipCount: return "invalid texture mip count";
		case TextureStructureValidationError::InvalidArrayConfiguration: return "invalid texture array configuration";
		case TextureStructureValidationError::InvalidSubresourceCount: return "invalid texture subresource count";
		case TextureStructureValidationError::InvalidSubresourceIndex: return "invalid texture subresource index";
		case TextureStructureValidationError::DuplicateSubresource: return "duplicate texture subresource";
		case TextureStructureValidationError::MissingSubresource: return "missing texture subresource";
		case TextureStructureValidationError::NonCanonicalSubresourceOrder: return "non-canonical texture subresource order";
		case TextureStructureValidationError::InvalidSubresourceExtent: return "invalid texture subresource extent";
		case TextureStructureValidationError::OffsetOverflow: return "texture subresource offset overflow";
		case TextureStructureValidationError::OutOfBounds: return "texture subresource is out of bounds";
		case TextureStructureValidationError::InvalidRowPitch: return "invalid texture row pitch";
		case TextureStructureValidationError::InvalidSlicePitch: return "invalid texture slice pitch";
		case TextureStructureValidationError::InvalidDataSize: return "invalid texture data size";
		case TextureStructureValidationError::ExceedsConfiguredLimit: return "texture exceeds configured validation limits";
		}
		return "unknown texture structure validation error";
	}

	std::string_view TextureUploadValidationDispositionText(
		TextureUploadValidationDisposition disposition) noexcept
	{
		switch (disposition)
		{
		case TextureUploadValidationDisposition::Valid: return "valid";
		case TextureUploadValidationDisposition::Corrupt: return "corrupt";
		case TextureUploadValidationDisposition::Unsupported: return "unsupported";
		}
		return "unknown";
	}

	TextureStructureValidationResult ValidateTextureAssetMetadata(
		const TextureAssetData& data,
		uint64_t declaredSubresourceCount,
		uint64_t declaredPixelBytes,
		TextureAssetValidationLimits limits) noexcept
	{
		const RHIFormatInfo& resourceFormat = GetRHIFormatInfo(data.m_ResourceFormat);
		const RHIFormatInfo& viewFormat = GetRHIFormatInfo(data.m_ViewFormat);
		if (resourceFormat.m_Format == RHIFormat::Unknown ||
			viewFormat.m_Format == RHIFormat::Unknown || viewFormat.m_IsTypeless ||
			!IsTextureAssetViewDimension(data.m_SrvDimension) ||
			(data.m_ColorSpace != TextureColorSpace::Linear &&
				data.m_ColorSpace != TextureColorSpace::SRGB))
		{
			return Error(TextureStructureValidationError::InvalidEnum);
		}
		if (!AreRHIFormatsInSameFamily(data.m_ResourceFormat, data.m_ViewFormat))
		{
			return Error(TextureStructureValidationError::IncompatibleFormats);
		}

		if (data.m_Extent.m_Width == 0 || data.m_Extent.m_Height == 0 ||
			data.m_Extent.m_Depth != 1)
		{
			return Error(TextureStructureValidationError::InvalidExtent);
		}
		if (data.m_ArraySize == 0)
		{
			return Error(TextureStructureValidationError::InvalidArrayConfiguration);
		}
		if (data.m_MipLevels == 0 ||
			data.m_MipLevels > std::bit_width(
				std::max(data.m_Extent.m_Width, data.m_Extent.m_Height)))
		{
			return Error(TextureStructureValidationError::InvalidMipCount);
		}

		if (data.m_Extent.m_Width > limits.m_MaxDimension ||
			data.m_Extent.m_Height > limits.m_MaxDimension ||
			data.m_ArraySize > limits.m_MaxArraySize ||
			declaredSubresourceCount > limits.m_MaxSubresources ||
			declaredPixelBytes > limits.m_MaxPixelBytes)
		{
			return Error(TextureStructureValidationError::ExceedsConfiguredLimit);
		}

		switch (data.m_SrvDimension)
		{
		case RHITextureViewDimension::Texture2D:
			if (data.m_ArraySize != 1)
			{
				return Error(TextureStructureValidationError::InvalidArrayConfiguration);
			}
			break;
		case RHITextureViewDimension::Texture2DArray:
			if (data.m_ArraySize <= 1)
			{
				return Error(TextureStructureValidationError::InvalidArrayConfiguration);
			}
			break;
		case RHITextureViewDimension::TextureCube:
			if (data.m_ArraySize != CubemapFaceCount ||
				data.m_Extent.m_Width != data.m_Extent.m_Height)
			{
				return Error(TextureStructureValidationError::InvalidArrayConfiguration);
			}
			break;
		case RHITextureViewDimension::TextureCubeArray:
			if (data.m_ArraySize < CubemapFaceCount ||
				(data.m_ArraySize % CubemapFaceCount) != 0 ||
				data.m_Extent.m_Width != data.m_Extent.m_Height)
			{
				return Error(TextureStructureValidationError::InvalidArrayConfiguration);
			}
			break;
		default:
			return Error(TextureStructureValidationError::InvalidEnum);
		}

		const uint64_t expectedSubresources =
			static_cast<uint64_t>(data.m_ArraySize) * data.m_MipLevels;
		if (declaredSubresourceCount != expectedSubresources)
		{
			return Error(TextureStructureValidationError::InvalidSubresourceCount);
		}
		if (declaredPixelBytes == 0)
		{
			return Error(TextureStructureValidationError::InvalidDataSize);
		}

		if (const auto result = ValidateRHITextureDesc(BuildTextureRHITextureDesc(data));
			!result.IsValid())
		{
			return MapRHIValidationError(result.m_Error);
		}
		if (const auto result = ValidateRHITextureViewDesc(
			BuildTextureRHITextureDesc(data), BuildTextureRHISRVDesc(data));
			!result.IsValid())
		{
			return MapRHIValidationError(result.m_Error);
		}
		return {};
	}

	TextureStructureValidationResult ValidateTextureAssetStructure(
		const TextureAssetData& data,
		TextureAssetValidationLimits limits) noexcept
	{
		if (const auto result = ValidateTextureAssetMetadata(
			data,
			static_cast<uint64_t>(data.m_Subresources.size()),
			static_cast<uint64_t>(data.m_Pixels.size()),
			limits); !result.IsValid())
		{
			return result;
		}

		const size_t expectedSubresources = data.m_Subresources.size();
		std::vector<uint8_t> seen(expectedSubresources, 0);
		const RHIFormatInfo& formatInfo = GetRHIFormatInfo(data.m_ResourceFormat);
		uint64_t expectedDataOffset = 0;

		for (size_t ordinal = 0; ordinal < data.m_Subresources.size(); ++ordinal)
		{
			const TextureAssetSubresource& subresource = data.m_Subresources[ordinal];
			if (subresource.m_MipLevel >= data.m_MipLevels ||
				subresource.m_ArraySlice >= data.m_ArraySize)
			{
				return Error(TextureStructureValidationError::InvalidSubresourceIndex);
			}
			const size_t subresourceIndex = static_cast<size_t>(subresource.m_ArraySlice) *
				data.m_MipLevels + subresource.m_MipLevel;
			if (seen[subresourceIndex] != 0)
			{
				return Error(TextureStructureValidationError::DuplicateSubresource);
			}
			if (subresourceIndex != ordinal)
			{
				return Error(TextureStructureValidationError::NonCanonicalSubresourceOrder);
			}
			seen[subresourceIndex] = 1;

			const uint32_t expectedWidth =
				std::max(1u, data.m_Extent.m_Width >> subresource.m_MipLevel);
			const uint32_t expectedHeight =
				std::max(1u, data.m_Extent.m_Height >> subresource.m_MipLevel);
			if (subresource.m_Width != expectedWidth ||
				subresource.m_Height != expectedHeight || subresource.m_Depth != 1)
			{
				return Error(TextureStructureValidationError::InvalidSubresourceExtent);
			}

			if (subresource.m_DataOffset >
				std::numeric_limits<uint64_t>::max() - subresource.m_DataSize)
			{
				return Error(TextureStructureValidationError::OffsetOverflow);
			}
			const uint64_t dataEnd = subresource.m_DataOffset + subresource.m_DataSize;
			if (dataEnd > data.m_Pixels.size())
			{
				return Error(TextureStructureValidationError::OutOfBounds);
			}

			const uint64_t blockColumns =
				(expectedWidth + formatInfo.m_BlockWidth - 1) / formatInfo.m_BlockWidth;
			const uint64_t blockRows =
				(expectedHeight + formatInfo.m_BlockHeight - 1) / formatInfo.m_BlockHeight;
			const uint64_t minimumRowPitch = blockColumns * formatInfo.m_BytesPerBlock;
			if (subresource.m_RowPitch < minimumRowPitch)
			{
				return Error(TextureStructureValidationError::InvalidRowPitch);
			}
			if (blockRows > std::numeric_limits<uint64_t>::max() / subresource.m_RowPitch ||
				subresource.m_SlicePitch < subresource.m_RowPitch * blockRows)
			{
				return Error(TextureStructureValidationError::InvalidSlicePitch);
			}
			if (subresource.m_DataSize != subresource.m_SlicePitch)
			{
				return Error(TextureStructureValidationError::InvalidDataSize);
			}
			if (subresource.m_DataOffset != expectedDataOffset)
			{
				return Error(TextureStructureValidationError::NonCanonicalSubresourceOrder);
			}
			expectedDataOffset = dataEnd;
		}

		if (std::ranges::find(seen, uint8_t{ 0 }) != seen.end())
		{
			return Error(TextureStructureValidationError::MissingSubresource);
		}
		if (expectedDataOffset != data.m_Pixels.size())
		{
			return Error(TextureStructureValidationError::InvalidDataSize);
		}

		const RHITextureValidationResult uploadValidation = ValidateRHITextureUploadData(
			BuildTextureRHITextureDesc(data), BuildTextureUploadDataUnchecked(data));
		if (!uploadValidation.IsValid() &&
			uploadValidation.m_Error != RHITextureValidationError::UnsupportedUploadFormat)
		{
			return MapRHIValidationError(uploadValidation.m_Error);
		}
		return {};
	}

	RHITextureDesc BuildTextureRHITextureDesc(const TextureAssetData& data) noexcept
	{
		return
		{
			.m_Dimension = RHITextureDimension::Texture2D,
			.m_Format = data.m_ResourceFormat,
			.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::CopyDest,
			.m_Extent = data.m_Extent,
			.m_ArraySize = data.m_ArraySize,
			.m_MipLevels = data.m_MipLevels,
			.m_SampleCount = 1,
		};
	}

	RHITextureViewDesc BuildTextureRHISRVDesc(const TextureAssetData& data) noexcept
	{
		return
		{
			.m_Type = RHITextureViewType::ShaderResource,
			.m_Dimension = data.m_SrvDimension,
			.m_Format = data.m_ViewFormat,
			.m_Subresources =
			{
				.m_BaseMip = 0,
				.m_MipCount = data.m_MipLevels,
				.m_BaseArraySlice = 0,
				.m_ArraySliceCount = data.m_ArraySize,
			},
		};
	}

	TextureUploadValidationResult ValidateTextureUploadForDevice(
		const TextureAssetData& data,
		const RHIDevice& device,
		TextureAssetValidationLimits limits) noexcept
	{
		const TextureStructureValidationResult structure =
			ValidateTextureAssetStructure(data, limits);
		if (!structure.IsValid())
		{
			return
			{
				.m_Disposition = TextureUploadValidationDisposition::Corrupt,
				.m_StructureError = structure.m_Error,
			};
		}

		const RHITextureDesc textureDesc = BuildTextureRHITextureDesc(data);
		const RHITextureViewDesc viewDesc = BuildTextureRHISRVDesc(data);
		const RHITextureValidationResult uploadValidation = ValidateRHITextureUploadData(
			textureDesc, BuildTextureUploadDataUnchecked(data));
		if (!uploadValidation.IsValid())
		{
			return
			{
				.m_Disposition = uploadValidation.m_Error ==
					RHITextureValidationError::UnsupportedUploadFormat ?
					TextureUploadValidationDisposition::Unsupported :
					TextureUploadValidationDisposition::Corrupt,
				.m_RHIError = uploadValidation.m_Error,
			};
		}

		const RHITextureSupportResult textureSupport = device.QueryTextureSupport(textureDesc);
		if (!textureSupport.IsSupported())
		{
			return
			{
				.m_Disposition = textureSupport.IsDescriptionValid() ?
					TextureUploadValidationDisposition::Unsupported :
					TextureUploadValidationDisposition::Corrupt,
				.m_RHIError = textureSupport.m_ValidationError,
			};
		}
		const RHITextureSupportResult viewSupport =
			device.QueryTextureViewSupport(textureDesc, viewDesc);
		if (!viewSupport.IsSupported())
		{
			return
			{
				.m_Disposition = viewSupport.IsDescriptionValid() ?
					TextureUploadValidationDisposition::Unsupported :
					TextureUploadValidationDisposition::Corrupt,
				.m_RHIError = viewSupport.m_ValidationError,
			};
		}

		return { .m_Disposition = TextureUploadValidationDisposition::Valid };
	}
}
