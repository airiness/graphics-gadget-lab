#pragma once
#include "Graphics/RHI/RHITypes.h"

#include <vulkan/vulkan.h>

#include <optional>

namespace gglab
{
	[[nodiscard]] VkPipelineStageFlags2 ToVulkanPipelineStages(RHIStage stages) noexcept;
	[[nodiscard]] VkAccessFlags2 ToVulkanAccessFlags(RHIAccess access) noexcept;
	[[nodiscard]] std::optional<VkImageLayout> ToVulkanImageLayout(RHILayout layout) noexcept;
}
