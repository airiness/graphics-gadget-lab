#include "Core/Precompiled.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanTimelineFence.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <array>
#include <format>
#include <vector>

namespace gglab
{
	namespace
	{
		constexpr std::string_view SwapchainExtensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
		constexpr std::string_view MutableDescriptorTypeExtensionName =
			VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME;
	}

	VulkanDevice::~VulkanDevice()
	{
		Destroy();
	}

	VulkanDevice::Result VulkanDevice::Create(const CreateInfo& createInfo) noexcept
	{
		Result result{};
		if (createInfo.m_Instance == VK_NULL_HANDLE ||
			createInfo.m_PhysicalDevice == VK_NULL_HANDLE || !createInfo.m_ProfileCapabilities)
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			result.m_Error =
				"VulkanDevice requires an instance, a physical device and a capability snapshot.";
			return result;
		}
		const VulkanDeviceProfileCapabilities& capabilities = *createInfo.m_ProfileCapabilities;

		std::array<const char*, 2> enabledExtensions{
			SwapchainExtensionName.data(),
			MutableDescriptorTypeExtensionName.data(),
		};

		VkPhysicalDeviceFeatures2 features2{};
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.features.samplerAnisotropy = capabilities.m_SamplerAnisotropy ? VK_TRUE : VK_FALSE;
		features2.features.shaderStorageImageExtendedFormats =
			capabilities.m_ShaderStorageImageExtendedFormats ? VK_TRUE : VK_FALSE;

		VkPhysicalDeviceVulkan13Features vulkan13Features{};
		vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		vulkan13Features.dynamicRendering = capabilities.m_DynamicRendering ? VK_TRUE : VK_FALSE;
		vulkan13Features.synchronization2 = capabilities.m_Synchronization2 ? VK_TRUE : VK_FALSE;
		features2.pNext = &vulkan13Features;

		VkPhysicalDeviceVulkan12Features vulkan12Features{};
		vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		vulkan12Features.timelineSemaphore = capabilities.m_TimelineSemaphore ? VK_TRUE : VK_FALSE;
		vulkan12Features.scalarBlockLayout = capabilities.m_ScalarBlockLayout ? VK_TRUE : VK_FALSE;
		vulkan12Features.runtimeDescriptorArray =
			capabilities.m_RuntimeDescriptorArray ? VK_TRUE : VK_FALSE;
		vulkan12Features.descriptorBindingPartiallyBound =
			capabilities.m_DescriptorBindingPartiallyBound ? VK_TRUE : VK_FALSE;
		vulkan12Features.descriptorBindingUpdateUnusedWhilePending =
			capabilities.m_DescriptorBindingUpdateUnusedWhilePending ? VK_TRUE : VK_FALSE;
		vulkan12Features.descriptorBindingSampledImageUpdateAfterBind =
			capabilities.m_DescriptorBindingSampledImageUpdateAfterBind ? VK_TRUE : VK_FALSE;
		vulkan12Features.descriptorBindingStorageImageUpdateAfterBind =
			capabilities.m_DescriptorBindingStorageImageUpdateAfterBind ? VK_TRUE : VK_FALSE;
		vulkan12Features.shaderSampledImageArrayNonUniformIndexing =
			capabilities.m_ShaderSampledImageArrayNonUniformIndexing ? VK_TRUE : VK_FALSE;
		vulkan12Features.shaderStorageImageArrayNonUniformIndexing =
			capabilities.m_ShaderStorageImageArrayNonUniformIndexing ? VK_TRUE : VK_FALSE;
		vulkan13Features.pNext = &vulkan12Features;

		VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableDescriptorFeatures{};
		mutableDescriptorFeatures.sType =
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT;
		mutableDescriptorFeatures.mutableDescriptorType =
			capabilities.m_MutableDescriptorType ? VK_TRUE : VK_FALSE;
		vulkan12Features.pNext = &mutableDescriptorFeatures;

		const float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = createInfo.m_GraphicsPresentQueueFamilyIndex;
		// Exactly one graphics/present queue is requested from the selected
		// family regardless of the family's total queue count.
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		VkDeviceCreateInfo deviceCreateInfo{};
		deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		deviceCreateInfo.queueCreateInfoCount = 1;
		deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
		deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
		deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
		deviceCreateInfo.pNext = &features2;

		auto device = std::make_unique<VulkanDevice>();
		device->m_Instance = createInfo.m_Instance;
		device->m_PhysicalDevice = createInfo.m_PhysicalDevice;
		device->m_PortabilityCapabilities = createInfo.m_PortabilityCapabilities;
		const VkResult createResult =
			vkCreateDevice(createInfo.m_PhysicalDevice, &deviceCreateInfo, nullptr, &device->m_Device);
		if (createResult != VK_SUCCESS)
		{
			result.m_Result = createResult;
			result.m_Error = std::format("vkCreateDevice failed with {}.", ToString(createResult));
			return result;
		}
		device->m_QueueFamilyIndex = createInfo.m_GraphicsPresentQueueFamilyIndex;

		vkGetDeviceQueue(device->m_Device, createInfo.m_GraphicsPresentQueueFamilyIndex, 0,
			&device->m_GraphicsQueue);
		if (device->m_GraphicsQueue == VK_NULL_HANDLE)
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			result.m_Error = "vkGetDeviceQueue returned a null graphics queue.";
			return result;
		}

		// VMA owns raw memory allocation only; the resource manager owns
		// every GGLab handle, native object, view and lifetime decision.
		VmaAllocatorCreateInfo allocatorCreateInfo{};
		allocatorCreateInfo.physicalDevice = createInfo.m_PhysicalDevice;
		allocatorCreateInfo.device = device->m_Device;
		allocatorCreateInfo.instance = createInfo.m_Instance;
		allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
		const VkResult allocatorResult =
			vmaCreateAllocator(&allocatorCreateInfo, &device->m_MemAllocator);
		if (allocatorResult != VK_SUCCESS)
		{
			result.m_Result = allocatorResult;
			result.m_Error = std::format("vmaCreateAllocator failed with {}.", ToString(allocatorResult));
			return result;
		}

		device->m_ResourceManager.Initialize(device.get());

		result.m_Device = std::move(device);
		return result;
	}

	bool VulkanDevice::IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept
	{
		if (!fencePoint.IsValid())
		{
			return true;
		}
		if (m_GraphicsTimeline == nullptr ||
			fencePoint.m_Fence != m_GraphicsTimeline->GetRHIHandle())
		{
			// An unknown fence never counts as completed.
			return false;
		}
		uint64_t completedValue = 0;
		if (m_GraphicsTimeline->GetCompletedValue(completedValue) != VK_SUCCESS)
		{
			return false;
		}
		return completedValue >= fencePoint.m_Value;
	}

	void VulkanDevice::Destroy() noexcept
	{
		// The resource manager releases its native objects and VMA
		// allocations before the allocator and the device go away. The
		// frame runtime must already be destroyed (it owns the timeline
		// this device only borrows).
		if (m_MemAllocator != VK_NULL_HANDLE)
		{
			m_ResourceManager.Finalize();
			vmaDestroyAllocator(m_MemAllocator);
			m_MemAllocator = VK_NULL_HANDLE;
		}
		m_GraphicsTimeline = nullptr;
		if (m_Device != VK_NULL_HANDLE)
		{
			vkDestroyDevice(m_Device, nullptr);
			m_Device = VK_NULL_HANDLE;
		}
		m_GraphicsQueue = VK_NULL_HANDLE;
	}
}
