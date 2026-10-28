#pragma once
#include "Graphics/RHI/AccelerationStructureStore.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"

#include <vulkan/vulkan.h>
#include <optional>

namespace gglab
{
	class VulkanDevice;
	struct VulkanAccelerationStructureBacking final : RHIAccelerationStructureBackingBase
	{
		~VulkanAccelerationStructureBacking() override;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkAccelerationStructureKHR m_Handle = VK_NULL_HANDLE;
		VkDeviceAddress m_Address = 0;
		PFN_vkDestroyAccelerationStructureKHR m_Destroy = nullptr;
	};

	class VulkanAccelerationStructureManager
	{
	public:
		explicit VulkanAccelerationStructureManager(VulkanDevice& device) noexcept : m_Device(device) {}
		[[nodiscard]] std::optional<RHIASBuildSizes> Query(const RHIASBuildInputs& inputs) noexcept;
		[[nodiscard]] RHIAccelerationStructureHandle Create(const RHIAccelerationStructureDesc& desc) noexcept;
		void Destroy(RHIAccelerationStructureHandle handle) noexcept;
		[[nodiscard]] bool IsAlive(RHIAccelerationStructureHandle handle) const noexcept
		{
			return m_Store.Resolve(handle) != nullptr;
		}
		[[nodiscard]] RHIAllocationFacts QueryAllocationFacts(RHIAccelerationStructureHandle handle) const noexcept;
		[[nodiscard]] bool Build(RHICommandContext& context, VkCommandBuffer commands,
			RHIAccelerationStructureHandle destination, const RHIASBuildInputs& inputs,
			const RHIASBufferRange& scratch) noexcept;
		[[nodiscard]] VkAccelerationStructureKHR Bind(RHICommandContext& context,
			RHIAccelerationStructureHandle handle) noexcept;
		[[nodiscard]] std::shared_ptr<VulkanAccelerationStructureBacking> Resolve(
			RHICommandContext& context, RHIAccelerationStructureHandle handle) noexcept;

	private:
		[[nodiscard]] VkDeviceAddress BufferAddress(RHIBufferHandle handle) noexcept;
		[[nodiscard]] bool Describe(const RHIASBuildInputs& inputs,
			VkAccelerationStructureBuildGeometryInfoKHR& native,
			VkAccelerationStructureGeometryKHR& geometry, uint32_t& primitiveCount) noexcept;
		VulkanDevice& m_Device;
		RHIAccelerationStructureStore m_Store;
	};
}
