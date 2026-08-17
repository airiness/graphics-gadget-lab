#pragma once
#include "Graphics/RHI/RHICommandContext.h"

#include <vulkan/vulkan.h>

#include <optional>

namespace gglab
{
	[[nodiscard]] VkPipelineStageFlags2 ToVulkanPipelineStages(RHIStage stages) noexcept;
	[[nodiscard]] VkAccessFlags2 ToVulkanAccessFlags(RHIAccess access) noexcept;
	[[nodiscard]] std::optional<VkImageLayout> ToVulkanImageLayout(RHILayout layout) noexcept;
	[[nodiscard]] std::optional<VkImageMemoryBarrier2> BuildVulkanTextureBarrier(
		const RHITextureBarrier& barrier, VkImage image, const RHITextureDesc& desc) noexcept;
	[[nodiscard]] std::optional<VkBufferMemoryBarrier2> BuildVulkanBufferBarrier(
		const RHIBufferBarrier& barrier, VkBuffer buffer) noexcept;
	[[nodiscard]] VkImageMemoryBarrier2 MakeVulkanImageBarrier(VkImage image,
		const VkImageSubresourceRange& subresources, VkImageLayout oldLayout,
		VkImageLayout newLayout, VkPipelineStageFlags2 sourceStages,
		VkAccessFlags2 sourceAccess, VkPipelineStageFlags2 destinationStages,
		VkAccessFlags2 destinationAccess) noexcept;
	[[nodiscard]] VkImageMemoryBarrier2 MakeVulkanImageBarrier(VkImage image,
		VkImageAspectFlags aspects, VkImageLayout oldLayout, VkImageLayout newLayout,
		VkPipelineStageFlags2 sourceStages, VkAccessFlags2 sourceAccess,
		VkPipelineStageFlags2 destinationStages, VkAccessFlags2 destinationAccess) noexcept;
}
