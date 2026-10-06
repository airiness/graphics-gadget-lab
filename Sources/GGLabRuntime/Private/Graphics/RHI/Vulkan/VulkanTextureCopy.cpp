#include "Graphics/RHI/Vulkan/VulkanTextureCopy.h"
#include "GGLabFoundation/Base/MathUtils.h"
#include "Graphics/RHI/RHISubresourceUtils.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureValidation.h"
#include "Graphics/RHI/Vulkan/VulkanFormat.h"

#include <algorithm>
#include <limits>
#include <numeric>

namespace gglab
{
	std::optional<VulkanTextureCopyLayout> BuildVulkanTextureCopyLayout(
		const RHITextureDesc& desc, VkDeviceSize requiredOffsetAlignment) noexcept
	{
		const RHIFormatInfo& formatInfo = GetRHIFormatInfo(desc.m_Format);
		if (formatInfo.m_PlaneCount != 1 || formatInfo.m_BytesPerBlock == 0 ||
			formatInfo.m_BlockWidth == 0 || formatInfo.m_BlockHeight == 0)
		{
			return std::nullopt;
		}

		const uint64_t offsetAlignment = std::lcm<uint64_t>(
			std::lcm<uint64_t>(4, formatInfo.m_BytesPerBlock),
			std::max<VkDeviceSize>(requiredOffsetAlignment, 1));
		const uint32_t mipLevels = GetRHITextureMipLevelCount(desc);
		const uint32_t arraySize = GetRHITextureArraySize(desc);
		VulkanTextureCopyLayout result{};
		result.m_Regions.reserve(static_cast<size_t>(mipLevels) * arraySize);
		result.m_Subresources.reserve(static_cast<size_t>(mipLevels) * arraySize);

		for (uint32_t arraySlice = 0; arraySlice < arraySize; ++arraySlice)
		{
			for (uint32_t mipLevel = 0; mipLevel < mipLevels; ++mipLevel)
			{
				const uint32_t width = std::max(1u, desc.m_Extent.m_Width >> mipLevel);
				const uint32_t height = desc.m_Dimension == RHITextureDimension::Texture1D
					? 1u
					: std::max(1u, desc.m_Extent.m_Height >> mipLevel);
				const uint32_t depth = desc.m_Dimension == RHITextureDimension::Texture3D
					? std::max(1u, desc.m_Extent.m_Depth >> mipLevel)
					: 1u;
				const uint64_t blockColumns =
					(width + formatInfo.m_BlockWidth - 1) / formatInfo.m_BlockWidth;
				const uint64_t blockRows =
					(height + formatInfo.m_BlockHeight - 1) / formatInfo.m_BlockHeight;
				const uint64_t rowSize = blockColumns * formatInfo.m_BytesPerBlock;
				if (blockRows > std::numeric_limits<uint64_t>::max() / rowSize)
				{
					return std::nullopt;
				}
				const uint64_t slicePitch = rowSize * blockRows;
				if (depth > std::numeric_limits<uint64_t>::max() / slicePitch)
				{
					return std::nullopt;
				}
				const uint64_t dataSize = slicePitch * depth;
				const uint64_t offset = utils::AlignUp(result.m_TotalBytes, offsetAlignment);
				if (offset > std::numeric_limits<uint64_t>::max() - dataSize)
				{
					return std::nullopt;
				}

				VkBufferImageCopy2 region{};
				region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
				region.bufferOffset = offset;
				region.bufferRowLength = 0;
				region.bufferImageHeight = 0;
				region.imageSubresource.aspectMask =
					ToVulkanImageAspectFlags(GetRHITextureAspects(desc));
				region.imageSubresource.mipLevel = mipLevel;
				region.imageSubresource.baseArrayLayer =
					desc.m_Dimension == RHITextureDimension::Texture3D ? 0 : arraySlice;
				region.imageSubresource.layerCount = 1;
				region.imageExtent = { width, height, depth };
				result.m_Regions.push_back(region);
				result.m_Subresources.push_back({
					.m_BufferOffset = offset,
					.m_RowPitch = rowSize,
					.m_RowSizeInBytes = rowSize,
					.m_SlicePitch = slicePitch,
					.m_RowCount = static_cast<uint32_t>(blockRows),
					.m_Width = width,
					.m_Height = height,
					.m_Depth = depth,
					.m_MipLevel = mipLevel,
					.m_ArraySlice = arraySlice,
				});
				result.m_TotalBytes = offset + dataSize;
			}
		}
		return result;
	}

	std::optional<VkBufferImageCopy2> BuildVulkanTextureToBufferCopyRegion(
		const RHITextureDesc& sourceDesc, const RHIBufferDesc& destinationDesc,
		const RHITextureToBufferCopy& copy) noexcept
	{
		const RHITextureCopyFootprint& footprint = copy.m_Footprint;
		const uint32_t mipWidth =
			std::max(sourceDesc.m_Extent.m_Width >> copy.m_SourceMipLevel, 1u);
		const uint32_t mipHeight =
			std::max(sourceDesc.m_Extent.m_Height >> copy.m_SourceMipLevel, 1u);
		if (!footprint.IsValid() || sourceDesc.m_Dimension != RHITextureDimension::Texture2D ||
			sourceDesc.m_SampleCount != 1 || sourceDesc.m_Format != footprint.m_Format ||
			!Test(sourceDesc.m_Usage, RHITextureUsage::CopySource) ||
			copy.m_SourceMipLevel >= GetRHITextureMipLevelCount(sourceDesc) ||
			copy.m_SourceArraySlice >= GetRHITextureArraySize(sourceDesc) ||
			footprint.m_Width > mipWidth || footprint.m_Height > mipHeight ||
			!Test(destinationDesc.m_Usage, RHIBufferUsage::CopyDest) ||
			copy.m_DestinationOffset % RHITextureCopyPlacementAlignment != 0 ||
			footprint.m_SizeInBytes > destinationDesc.m_SizeInBytes ||
			copy.m_DestinationOffset > destinationDesc.m_SizeInBytes - footprint.m_SizeInBytes ||
			footprint.m_RowPitch % footprint.m_BytesPerTexel != 0)
		{
			return std::nullopt;
		}

		return VkBufferImageCopy2{
			.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
			.bufferOffset = copy.m_DestinationOffset,
			.bufferRowLength =
				static_cast<uint32_t>(footprint.m_RowPitch / footprint.m_BytesPerTexel),
			.bufferImageHeight = footprint.m_Height,
			.imageSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = copy.m_SourceMipLevel,
				.baseArrayLayer = copy.m_SourceArraySlice,
				.layerCount = 1,
			},
			.imageOffset = { 0, 0, 0 },
			.imageExtent = { footprint.m_Width, footprint.m_Height, 1 },
		};
	}
}
