#pragma once
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace gglab
{
	struct VulkanTextureCopyLayout
	{
		uint64_t m_TotalBytes = 0;
		std::vector<VkBufferImageCopy2> m_Regions;
		std::vector<RHITextureReadbackSubresource> m_Subresources;
	};

	[[nodiscard]] std::optional<VulkanTextureCopyLayout> BuildVulkanTextureCopyLayout(
		const RHITextureDesc& desc, VkDeviceSize requiredOffsetAlignment) noexcept;
}
