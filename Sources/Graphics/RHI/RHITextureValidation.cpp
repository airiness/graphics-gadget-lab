#include "Core/Precompiled.h"
#include "Graphics/RHI/RHITextureValidation.h"
#include "Graphics/RHI/RHISubresourceUtils.h"

#include <algorithm>
#include <bit>
#include <limits>

namespace gglab
{
	namespace
	{
		constexpr RHITextureUsage KnownTextureUsages =
			RHITextureUsage::Sampled |
			RHITextureUsage::RenderTarget |
			RHITextureUsage::DepthStencil |
			RHITextureUsage::UnorderedAccess |
			RHITextureUsage::CopySource |
			RHITextureUsage::CopyDest |
			RHITextureUsage::Present;

		[[nodiscard]] constexpr RHITextureValidationResult Error(
			RHITextureValidationError error) noexcept
		{
			return { .m_Error = error };
		}

		[[nodiscard]] constexpr bool IsDimensionCompatible(
			RHITextureDimension textureDimension,
			RHITextureViewDimension viewDimension) noexcept
		{
			if (viewDimension == RHITextureViewDimension::Unknown)
			{
				return true;
			}

			switch (textureDimension)
			{
			case RHITextureDimension::Texture1D:
				return viewDimension == RHITextureViewDimension::Texture1D ||
					viewDimension == RHITextureViewDimension::Texture1DArray;
			case RHITextureDimension::Texture2D:
				return viewDimension == RHITextureViewDimension::Texture2D ||
					viewDimension == RHITextureViewDimension::Texture2DArray ||
					viewDimension == RHITextureViewDimension::TextureCube ||
					viewDimension == RHITextureViewDimension::TextureCubeArray;
			case RHITextureDimension::Texture3D:
				return viewDimension == RHITextureViewDimension::Texture3D;
			}
			return false;
		}

		[[nodiscard]] constexpr RHITextureUsage RequiredUsage(
			RHITextureViewType viewType) noexcept
		{
			switch (viewType)
			{
			case RHITextureViewType::RenderTarget:
				return RHITextureUsage::RenderTarget;
			case RHITextureViewType::DepthStencil:
				return RHITextureUsage::DepthStencil;
			case RHITextureViewType::ShaderResource:
				return RHITextureUsage::Sampled;
			case RHITextureViewType::UnorderedAccess:
				return RHITextureUsage::UnorderedAccess;
			}
			return RHITextureUsage::None;
		}
	}

	std::string_view RHITextureValidationErrorText(RHITextureValidationError error) noexcept
	{
		switch (error)
		{
		case RHITextureValidationError::None: return "none";
		case RHITextureValidationError::InvalidFormat: return "invalid texture format";
		case RHITextureValidationError::InvalidUsage: return "invalid texture usage";
		case RHITextureValidationError::InvalidExtent: return "invalid texture extent";
		case RHITextureValidationError::InvalidArraySize: return "invalid texture array size";
		case RHITextureValidationError::InvalidMipLevelCount: return "invalid texture mip-level count";
		case RHITextureValidationError::InvalidSampleCount: return "invalid texture sample count";
		case RHITextureValidationError::InvalidDimension: return "invalid texture dimension";
		case RHITextureValidationError::InvalidClearValue: return "invalid texture clear value";
		case RHITextureValidationError::IncompatibleViewFormat: return "incompatible texture view format";
		case RHITextureValidationError::IncompatibleViewDimension: return "incompatible texture view dimension";
		case RHITextureValidationError::InvalidSubresourceRange: return "invalid texture subresource range";
		case RHITextureValidationError::UnsupportedUploadFormat: return "unsupported texture upload format";
		case RHITextureValidationError::InvalidUploadSubresourceCount: return "invalid texture upload subresource count";
		case RHITextureValidationError::InvalidUploadSubresourceData: return "invalid texture upload subresource data";
		case RHITextureValidationError::InvalidUploadRowPitch: return "invalid texture upload row pitch";
		case RHITextureValidationError::InvalidUploadSlicePitch: return "invalid texture upload slice pitch";
		}
		return "unknown texture validation error";
	}

	RHITextureValidationResult ValidateRHITextureDesc(const RHITextureDesc& desc) noexcept
	{
		const RHIFormatInfo& formatInfo = GetRHIFormatInfo(desc.m_Format);
		if (formatInfo.m_Format == RHIFormat::Unknown)
		{
			return Error(RHITextureValidationError::InvalidFormat);
		}

		const uint32_t usage = static_cast<uint32_t>(desc.m_Usage);
		const uint32_t knownUsages = static_cast<uint32_t>(KnownTextureUsages);
		if (desc.m_Usage == RHITextureUsage::None || (usage & ~knownUsages) != 0 ||
			(Test(desc.m_Usage, RHITextureUsage::RenderTarget) &&
				Test(desc.m_Usage, RHITextureUsage::DepthStencil)))
		{
			return Error(RHITextureValidationError::InvalidUsage);
		}

		if (desc.m_Extent.m_Width == 0 || desc.m_Extent.m_Height == 0 || desc.m_Extent.m_Depth == 0)
		{
			return Error(RHITextureValidationError::InvalidExtent);
		}
		if (desc.m_ArraySize == 0)
		{
			return Error(RHITextureValidationError::InvalidArraySize);
		}
		if (desc.m_MipLevels == 0)
		{
			return Error(RHITextureValidationError::InvalidMipLevelCount);
		}
		if (desc.m_SampleCount == 0)
		{
			return Error(RHITextureValidationError::InvalidSampleCount);
		}

		uint32_t largestMipDimension = desc.m_Extent.m_Width;
		switch (desc.m_Dimension)
		{
		case RHITextureDimension::Texture1D:
			if (desc.m_Extent.m_Height != 1 || desc.m_Extent.m_Depth != 1)
			{
				return Error(RHITextureValidationError::InvalidDimension);
			}
			break;
		case RHITextureDimension::Texture2D:
			if (desc.m_Extent.m_Depth != 1)
			{
				return Error(RHITextureValidationError::InvalidDimension);
			}
			largestMipDimension = std::max(desc.m_Extent.m_Width, desc.m_Extent.m_Height);
			break;
		case RHITextureDimension::Texture3D:
			if (desc.m_ArraySize != 1)
			{
				return Error(RHITextureValidationError::InvalidArraySize);
			}
			largestMipDimension = std::max({
				desc.m_Extent.m_Width, desc.m_Extent.m_Height, desc.m_Extent.m_Depth });
			break;
		default:
			return Error(RHITextureValidationError::InvalidDimension);
		}

		if (desc.m_MipLevels > std::bit_width(largestMipDimension))
		{
			return Error(RHITextureValidationError::InvalidMipLevelCount);
		}
		if (desc.m_SampleCount > 1 &&
			(desc.m_Dimension != RHITextureDimension::Texture2D || desc.m_MipLevels != 1 ||
				Test(desc.m_Usage, RHITextureUsage::UnorderedAccess)))
		{
			return Error(RHITextureValidationError::InvalidSampleCount);
		}

		if (Test(desc.m_Usage, RHITextureUsage::DepthStencil) &&
			formatInfo.m_DepthStencilAspects == RHITextureAspect::None)
		{
			return Error(RHITextureValidationError::InvalidUsage);
		}
		if ((Test(desc.m_Usage, RHITextureUsage::RenderTarget) ||
			Test(desc.m_Usage, RHITextureUsage::UnorderedAccess)) &&
			!Test(formatInfo.m_Aspects, RHITextureAspect::Color))
		{
			return Error(RHITextureValidationError::InvalidUsage);
		}

		if (desc.m_ClearValue)
		{
			const RHIClearValue& clearValue = *desc.m_ClearValue;
			if ((!Test(desc.m_Usage, RHITextureUsage::RenderTarget) &&
				!Test(desc.m_Usage, RHITextureUsage::DepthStencil)) ||
				clearValue.m_Format == RHIFormat::Unknown ||
				GetRHIFormatInfo(clearValue.m_Format).m_IsTypeless ||
				!AreRHIFormatsInSameFamily(desc.m_Format, clearValue.m_Format) ||
				(clearValue.m_IsDepthStencil != Test(desc.m_Usage, RHITextureUsage::DepthStencil)))
			{
				return Error(RHITextureValidationError::InvalidClearValue);
			}
		}

		return {};
	}

	RHITextureValidationResult ValidateRHITextureViewDesc(
		const RHITextureDesc& textureDesc,
		const RHITextureViewDesc& viewDesc) noexcept
	{
		if (const auto result = ValidateRHITextureDesc(textureDesc); !result.IsValid())
		{
			return result;
		}

		const RHIFormat viewFormat = viewDesc.m_Format == RHIFormat::Unknown ?
			textureDesc.m_Format : viewDesc.m_Format;
		const RHIFormatInfo& viewFormatInfo = GetRHIFormatInfo(viewFormat);
		if (viewFormatInfo.m_Format == RHIFormat::Unknown || viewFormatInfo.m_IsTypeless ||
			!AreRHIFormatsInSameFamily(textureDesc.m_Format, viewFormat))
		{
			return Error(RHITextureValidationError::IncompatibleViewFormat);
		}

		if (!Test(textureDesc.m_Usage, RequiredUsage(viewDesc.m_Type)))
		{
			return Error(RHITextureValidationError::InvalidUsage);
		}
		if (!IsDimensionCompatible(textureDesc.m_Dimension, viewDesc.m_Dimension))
		{
			return Error(RHITextureValidationError::IncompatibleViewDimension);
		}
		if (viewDesc.m_Type == RHITextureViewType::DepthStencil &&
			textureDesc.m_Dimension == RHITextureDimension::Texture3D)
		{
			return Error(RHITextureValidationError::IncompatibleViewDimension);
		}

		const RHISubresourceRange range = NormalizeTextureSubresourceRange(
			textureDesc, viewDesc.m_Subresources);
		if (range.m_MipCount == 0 || range.m_ArraySliceCount == 0 ||
			range.m_Aspects == RHITextureAspect::None)
		{
			return Error(RHITextureValidationError::InvalidSubresourceRange);
		}
		if (viewDesc.m_Dimension == RHITextureViewDimension::TextureCube &&
			range.m_ArraySliceCount != 6)
		{
			return Error(RHITextureValidationError::InvalidSubresourceRange);
		}
		if (viewDesc.m_Dimension == RHITextureViewDimension::TextureCubeArray &&
			(range.m_ArraySliceCount < 6 || range.m_ArraySliceCount % 6 != 0))
		{
			return Error(RHITextureValidationError::InvalidSubresourceRange);
		}

		if (viewDesc.m_Type == RHITextureViewType::DepthStencil &&
			viewFormatInfo.m_DepthStencilAspects == RHITextureAspect::None)
		{
			return Error(RHITextureValidationError::IncompatibleViewFormat);
		}
		if ((viewDesc.m_Type == RHITextureViewType::RenderTarget ||
			viewDesc.m_Type == RHITextureViewType::UnorderedAccess) &&
			!Test(viewFormatInfo.m_Aspects, RHITextureAspect::Color))
		{
			return Error(RHITextureValidationError::IncompatibleViewFormat);
		}

		return {};
	}

	RHITextureValidationResult ValidateRHITextureUploadData(
		const RHITextureDesc& textureDesc,
		const RHITextureUploadData& uploadData) noexcept
	{
		if (const auto result = ValidateRHITextureDesc(textureDesc); !result.IsValid())
		{
			return result;
		}

		const RHIFormatInfo& formatInfo = GetRHIFormatInfo(textureDesc.m_Format);
		if (textureDesc.m_SampleCount != 1 || formatInfo.m_PlaneCount != 1)
		{
			return Error(RHITextureValidationError::UnsupportedUploadFormat);
		}

		const size_t expectedSubresourceCount =
			static_cast<size_t>(textureDesc.m_MipLevels) * GetRHITextureArraySize(textureDesc);
		if (uploadData.m_Subresources.size() != expectedSubresourceCount)
		{
			return Error(RHITextureValidationError::InvalidUploadSubresourceCount);
		}

		for (size_t subresourceIndex = 0; subresourceIndex < expectedSubresourceCount; ++subresourceIndex)
		{
			const uint32_t mipLevel = static_cast<uint32_t>(subresourceIndex % textureDesc.m_MipLevels);
			const uint64_t width = std::max<uint32_t>(1, textureDesc.m_Extent.m_Width >> mipLevel);
			const uint64_t height = textureDesc.m_Dimension == RHITextureDimension::Texture1D ? 1 :
				std::max<uint32_t>(1, textureDesc.m_Extent.m_Height >> mipLevel);
			const uint64_t blockColumns = (width + formatInfo.m_BlockWidth - 1) / formatInfo.m_BlockWidth;
			const uint64_t blockRows = (height + formatInfo.m_BlockHeight - 1) / formatInfo.m_BlockHeight;
			const uint64_t minimumRowPitch = blockColumns * formatInfo.m_BytesPerBlock;
			const RHITextureSubresourceData& subresource = uploadData.m_Subresources[subresourceIndex];

			if (!subresource.m_Data)
			{
				return Error(RHITextureValidationError::InvalidUploadSubresourceData);
			}
			if (subresource.m_RowPitch < minimumRowPitch ||
				subresource.m_RowPitch > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
			{
				return Error(RHITextureValidationError::InvalidUploadRowPitch);
			}
			if (blockRows > std::numeric_limits<uint64_t>::max() / subresource.m_RowPitch ||
				subresource.m_SlicePitch < subresource.m_RowPitch * blockRows ||
				subresource.m_SlicePitch > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
			{
				return Error(RHITextureValidationError::InvalidUploadSlicePitch);
			}
		}

		return {};
	}
}
