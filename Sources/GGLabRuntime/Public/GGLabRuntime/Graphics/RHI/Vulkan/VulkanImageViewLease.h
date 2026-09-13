#pragma once
#include <vulkan/vulkan.h>

namespace gglab
{
	// Shared ownership pins an image view and its parent resource. A published
	// view keeps one object identity until replaced. Release only after GPU use.
	class VulkanImageViewLeaseBase
	{
	public:
		virtual ~VulkanImageViewLeaseBase() = default;
		[[nodiscard]] virtual VkImageView GetImageView() const noexcept = 0;
	};
}
