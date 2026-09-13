#pragma once

namespace gglab
{
	class VulkanContext;
	struct VulkanBackendSnapshot;

	void BuildVulkanBackendSnapshot(const VulkanContext& context,
		VulkanBackendSnapshot& outSnapshot) noexcept;
}
