#pragma once
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHITypes.h"
#include "Graphics/RHI/Vulkan/VulkanAdapter.h"
#include "Graphics/RHI/Vulkan/VulkanResourceManager.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <memory>
#include <string>

namespace gglab
{
	class VulkanTimelineFence;

	// Owns the VkDevice created for a profile-accepted adapter, the VMA
	// allocator and the resource subsystem. Only the graphics/present queue
	// is created; frame objects are produced by the frame runtime, which
	// borrows the device and registers its graphics timeline here so
	// resource retirement can resolve RHIFencePoints.
	class VulkanDevice
	{
	public:
		struct CreateInfo
		{
			VkInstance m_Instance = VK_NULL_HANDLE;
			VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
			// Feature availability captured by the capability snapshot; only
			// profile-required features are enabled on the device.
			const VulkanDeviceProfileCapabilities* m_ProfileCapabilities = nullptr;
			// Conditional portability capabilities for view/sampler
			// validation; they never gate device creation.
			RHIPortabilityCapabilities m_PortabilityCapabilities{};
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
		[[nodiscard]] VkInstance GetInstance() const noexcept { return m_Instance; }
		[[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const noexcept
		{
			return m_PhysicalDevice;
		}
		[[nodiscard]] VkQueue GetGraphicsQueue() const noexcept { return m_GraphicsQueue; }
		[[nodiscard]] uint32_t GetGraphicsQueueFamilyIndex() const noexcept
		{
			return m_QueueFamilyIndex;
		}

		[[nodiscard]] VmaAllocator GetMemAllocator() const noexcept { return m_MemAllocator; }
		[[nodiscard]] VulkanResourceManager& GetResourceManager() noexcept
		{
			return m_ResourceManager;
		}
		[[nodiscard]] const VulkanResourceManager& GetResourceManager() const noexcept
		{
			return m_ResourceManager;
		}
		[[nodiscard]] const RHIPortabilityCapabilities& GetPortabilityCapabilities() const noexcept
		{
			return m_PortabilityCapabilities;
		}

		// Registers the frame runtime's graphics timeline as the completion
		// source for RHIFencePoint resolution. Borrowed: the frame runtime
		// owns the timeline and must be destroyed before the device.
		void SetGraphicsTimeline(VulkanTimelineFence* timeline) noexcept
		{
			m_GraphicsTimeline = timeline;
		}
		[[nodiscard]] bool IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept;

	private:
		void Destroy() noexcept;

		VkInstance m_Instance = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		uint32_t m_QueueFamilyIndex = 0;
		RHIPortabilityCapabilities m_PortabilityCapabilities{};
		VulkanTimelineFence* m_GraphicsTimeline = nullptr;

		VmaAllocator m_MemAllocator = VK_NULL_HANDLE;
		VulkanResourceManager m_ResourceManager;
	};
}
