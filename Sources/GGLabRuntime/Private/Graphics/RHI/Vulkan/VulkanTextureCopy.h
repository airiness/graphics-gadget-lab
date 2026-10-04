#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
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

	// Validates one RHI texture-to-buffer copy against the live source and
	// destination descriptions and builds its native region.
	[[nodiscard]] std::optional<VkBufferImageCopy2> BuildVulkanTextureToBufferCopyRegion(
		const RHITextureDesc& sourceDesc, const RHIBufferDesc& destinationDesc,
		const RHITextureToBufferCopy& copy) noexcept;
}
