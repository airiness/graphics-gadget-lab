#pragma once
#include "Graphics/RHI/Vulkan/VulkanAdapter.h"

#include <vulkan/vulkan.h>

#include <memory>
#include <string>

namespace gglab
{
	// Owns the VkDevice created for a profile-accepted adapter. Only the
	// graphics/present queue is created; no frame objects are produced.
	class VulkanDevice
	{
	public:
		struct CreateInfo
		{
			VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
			// Feature availability captured by the capability snapshot; only
			// profile-required features are enabled on the device.
			const VulkanDeviceProfileCapabilities* m_ProfileCapabilities = nullptr;
			uint32_t m_GraphicsPresentQueueFamilyIndex = 0;
			uint32_t m_GraphicsPresentQueueCount = 1;
		};

		struct Result
		{
			std::unique_ptr<VulkanDevice> m_Device;
			std::string m_Error;
			VkResult m_Result = VK_SUCCESS;

			[[nodiscard]] bool Succeeded() const noexcept { return m_Device != nullptr; }
		};

	public:
		VulkanDevice() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanDevice);
		~VulkanDevice();

		[[nodiscard]] static Result Create(const CreateInfo& createInfo) noexcept;

		[[nodiscard]] VkDevice Get() const noexcept { return m_Device; }
		[[nodiscard]] VkQueue GetGraphicsQueue() const noexcept { return m_GraphicsQueue; }
		[[nodiscard]] uint32_t GetGraphicsQueueFamilyIndex() const noexcept
		{
			return m_QueueFamilyIndex;
		}

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		uint32_t m_QueueFamilyIndex = 0;
	};
}
