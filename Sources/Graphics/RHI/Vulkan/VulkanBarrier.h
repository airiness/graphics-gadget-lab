#pragma once
#include "Graphics/RHI/RHITypes.h"

#include <vulkan/vulkan.h>

#include <optional>

namespace gglab
{
	[[nodiscard]] VkPipelineStageFlags2 ToVulkanPipelineStages(RHIStage stages) noexcept;
	[[nodiscard]] VkAccessFlags2 ToVulkanAccessFlags(RHIAccess access) noexcept;
	[[nodiscard]] std::optional<VkImageLayout> ToVulkanImageLayout(RHILayout layout) noexcept;
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
