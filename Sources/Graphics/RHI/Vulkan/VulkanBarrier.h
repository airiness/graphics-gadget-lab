#pragma once
#include "Graphics/RHI/RHITypes.h"

#include <vulkan/vulkan.h>

namespace gglab
{
	[[nodiscard]] VkPipelineStageFlags2 ToVulkanPipelineStages(RHIStage stages) noexcept;
	[[nodiscard]] VkAccessFlags2 ToVulkanAccessFlags(RHIAccess access) noexcept;
	[[nodiscard]] VkImageLayout ToVulkanImageLayout(RHILayout layout) noexcept;
}
