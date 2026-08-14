#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/RHI/Vulkan/VulkanTimelineFence.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"
#include "GGLabFoundation/String/StringUtils.h"

#include <algorithm>
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

		[[nodiscard]] std::string BuildAdapterCompatibilityIdentity(
			const VulkanAdapterIdentity& identity) noexcept
		{
			return std::format("vulkan:{}:{}:{:08x}:{:08x}", identity.UuidHex(),
				utils::BytesToHexString(identity.m_DriverUuid), identity.m_ApiVersion,
				identity.m_DriverVersion);
		}
	}

	VulkanDevice::~VulkanDevice()
	{
		Destroy();
	}

	VulkanDevice::Result VulkanDevice::Create(const CreateInfo& createInfo) noexcept
	{
		Result result{};
		if (createInfo.m_Instance == VK_NULL_HANDLE ||
			createInfo.m_PhysicalDevice == VK_NULL_HANDLE || !createInfo.m_AdapterIdentity ||
			!createInfo.m_ProfileCapabilities)
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			result.m_Error =
				"VulkanDevice requires an instance, a physical device, adapter identity and capability snapshot.";
			return result;
		}
		const VulkanDeviceProfileCapabilities& capabilities = *createInfo.m_ProfileCapabilities;
		// The policy-reduced set is what the logical device enables and what
		// object creation may expose after the corresponding lowering exists.
		const RHIPortabilityCapabilities enabledCapabilities =
			ApplyVulkanPortabilityPolicy(createInfo.m_PortabilityCapabilities);

		std::array<const char*, 2> enabledExtensions{
			SwapchainExtensionName.data(),
			MutableDescriptorTypeExtensionName.data(),
		};

		VkPhysicalDeviceFeatures2 features2{};
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.features.samplerAnisotropy = capabilities.m_SamplerAnisotropy ? VK_TRUE : VK_FALSE;
		features2.features.shaderStorageImageExtendedFormats =
			capabilities.m_ShaderStorageImageExtendedFormats ? VK_TRUE : VK_FALSE;
		// Conditional core features the backend adopts: enabled when the
		// hardware reports them so future consuming layers find them ready.
		features2.features.imageCubeArray =
			createInfo.m_ImageCubeArrayAvailable ? VK_TRUE : VK_FALSE;
		features2.features.independentBlend =
			enabledCapabilities.m_IndependentBlend ? VK_TRUE : VK_FALSE;
		features2.features.depthClamp = enabledCapabilities.m_DepthClamp ? VK_TRUE : VK_FALSE;
		features2.features.depthBiasClamp =
			enabledCapabilities.m_DepthBiasClamp ? VK_TRUE : VK_FALSE;
		features2.features.fillModeNonSolid =
			enabledCapabilities.m_FillModeNonSolid ? VK_TRUE : VK_FALSE;

		VkPhysicalDeviceVulkan13Features vulkan13Features{};
		vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		vulkan13Features.dynamicRendering = capabilities.m_DynamicRendering ? VK_TRUE : VK_FALSE;
		vulkan13Features.synchronization2 = capabilities.m_Synchronization2 ? VK_TRUE : VK_FALSE;
		features2.pNext = &vulkan13Features;

		VkPhysicalDeviceVulkan12Features vulkan12Features{};
		vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		vulkan12Features.timelineSemaphore = capabilities.m_TimelineSemaphore ? VK_TRUE : VK_FALSE;
		vulkan12Features.scalarBlockLayout = capabilities.m_ScalarBlockLayout ? VK_TRUE : VK_FALSE;
		vulkan12Features.samplerMirrorClampToEdge =
			createInfo.m_SamplerMirrorClampToEdgeAvailable ? VK_TRUE : VK_FALSE;
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
		device->m_OwnerThreadId = std::this_thread::get_id();
		device->m_Instance = createInfo.m_Instance;
		device->m_PhysicalDevice = createInfo.m_PhysicalDevice;
		device->m_AdapterCompatibilityIdentity =
			BuildAdapterCompatibilityIdentity(*createInfo.m_AdapterIdentity);

		VkPhysicalDeviceSubgroupProperties subgroupProperties{};
		subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
		VkPhysicalDeviceProperties2 physicalDeviceProperties{};
		physicalDeviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		physicalDeviceProperties.pNext = &subgroupProperties;
		vkGetPhysicalDeviceProperties2(createInfo.m_PhysicalDevice, &physicalDeviceProperties);
		device->m_PhysicalDeviceLimits = physicalDeviceProperties.properties.limits;
		if (subgroupProperties.subgroupSize > 0)
		{
			device->m_ShaderWaveCapabilities = {
				.m_Supported = true,
				.m_MinLaneCount = subgroupProperties.subgroupSize,
				.m_MaxLaneCount = subgroupProperties.subgroupSize,
			};
		}
		device->m_EnabledPortabilityCapabilities = enabledCapabilities;
		device->m_ImageCubeArrayEnabled = createInfo.m_ImageCubeArrayAvailable;
		device->m_SamplerMirrorClampToEdgeEnabled =
			createInfo.m_SamplerMirrorClampToEdgeAvailable;
		device->m_PortabilityCapabilities = enabledCapabilities;
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

		if (!device->m_DescriptorManager.Initialize(device.get()))
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			result.m_Error = "Failed to initialize the Vulkan descriptor manager.";
			return result;
		}
		device->m_ResourceManager.Initialize(device.get());

		result.m_Device = std::move(device);
		return result;
	}

	bool VulkanDevice::RequireOwnerThread(std::string_view operation) const noexcept
	{
		if (IsOwnerThread())
		{
			return true;
		}
		GGLAB_LOG_GRAPHICS_ERROR(
			"{} rejected an off-owner Vulkan/RHI call.", operation);
		return false;
	}

	RHITextureSupportResult VulkanDevice::QueryTextureSupport(
		const RHITextureDesc& desc) const noexcept
	{
		return m_ResourceManager.QueryTextureSupport(desc);
	}

	RHITextureSupportResult VulkanDevice::QueryTextureViewSupport(
		const RHITextureDesc& textureDesc, const RHITextureViewDesc& viewDesc) const noexcept
	{
		const RHITextureValidationResult validation =
			ValidateRHITextureViewDesc(textureDesc, viewDesc);
		if (!validation.IsValid())
		{
			return { .m_ValidationError = validation.m_Error };
		}
		const RHIPortabilityValidationResult portability =
			ValidateRHITextureViewPortability(viewDesc, m_PortabilityCapabilities);
		if (!portability.IsValid())
		{
			return { .m_PortabilityError = portability.m_Error };
		}
		const RHITextureSupportResult textureSupport = QueryTextureSupport(textureDesc);
		if (!textureSupport.IsSupported())
		{
			return textureSupport;
		}
		const std::optional<VulkanNormalizedTextureView> normalized =
			NormalizeVulkanTextureView(textureDesc, viewDesc);
		if (!normalized ||
			!IsVulkanViewFormatCompatible(textureDesc.m_Format, normalized->m_EffectiveFormat))
		{
			return { .m_Reason = RHITextureSupportReason::FormatCombinationUnsupported };
		}
		if (normalized->m_EffectiveDimension == RHITextureViewDimension::TextureCubeArray &&
			!m_ImageCubeArrayEnabled)
		{
			return { .m_Reason = RHITextureSupportReason::TextureDimensionUnsupported };
		}
		return { .m_Supported = true };
	}

	RHITextureHandle VulkanDevice::CreateTexture(const RHIOwnedTextureCreateInfo& createInfo,
		const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		return RequireOwnerThread("VulkanDevice::CreateTexture")
			? m_ResourceManager.CreateTexture(createInfo, debugIdentity)
			: RHITextureHandle{};
	}

	RHIBufferHandle VulkanDevice::CreateBuffer(
		const RHIBufferDesc& desc, const RHIResourceDebugIdentityDesc& debugIdentity) noexcept
	{
		return RequireOwnerThread("VulkanDevice::CreateBuffer")
			? m_ResourceManager.CreateBuffer(desc, debugIdentity)
			: RHIBufferHandle{};
	}

	RHITextureViewHandle VulkanDevice::CreateTextureView(
		RHITextureHandle texture, const RHITextureViewDesc& desc) noexcept
	{
		return RequireOwnerThread("VulkanDevice::CreateTextureView")
			? m_ResourceManager.CreateTextureView(texture, desc)
			: RHITextureViewHandle{};
	}

	RHIBufferViewHandle VulkanDevice::CreateBufferView(
		RHIBufferHandle buffer, const RHIBufferViewDesc& desc) noexcept
	{
		return RequireOwnerThread("VulkanDevice::CreateBufferView")
			? m_ResourceManager.CreateBufferView(buffer, desc)
			: RHIBufferViewHandle{};
	}

	RHISamplerHandle VulkanDevice::CreateSampler(const RHISamplerDesc& desc) noexcept
	{
		return RequireOwnerThread("VulkanDevice::CreateSampler")
			? m_ResourceManager.CreateSampler(desc)
			: RHISamplerHandle{};
	}

	void VulkanDevice::DestroyTexture(RHITextureHandle texture) noexcept
	{
		if (RequireOwnerThread("VulkanDevice::DestroyTexture"))
		{
			m_ResourceManager.DestroyTexture(texture);
		}
	}

	void VulkanDevice::DestroyBuffer(RHIBufferHandle buffer) noexcept
	{
		if (RequireOwnerThread("VulkanDevice::DestroyBuffer"))
		{
			m_ResourceManager.DestroyBuffer(buffer);
		}
	}

	void VulkanDevice::DestroyTextureView(RHITextureViewHandle view) noexcept
	{
		if (RequireOwnerThread("VulkanDevice::DestroyTextureView"))
		{
			m_ResourceManager.DestroyTextureView(view);
		}
	}

	void VulkanDevice::DestroyBufferView(RHIBufferViewHandle view) noexcept
	{
		if (RequireOwnerThread("VulkanDevice::DestroyBufferView"))
		{
			m_ResourceManager.DestroyBufferView(view);
		}
	}

	void VulkanDevice::DestroySampler(RHISamplerHandle sampler) noexcept
	{
		if (RequireOwnerThread("VulkanDevice::DestroySampler"))
		{
			m_ResourceManager.DestroySampler(sampler);
		}
	}

	void VulkanDevice::SetTextureDebugBinding(
		RHITextureHandle texture, const RHIResourceDebugBindingDesc& binding) noexcept
	{
		if (RequireOwnerThread("VulkanDevice::SetTextureDebugBinding"))
		{
			m_ResourceManager.SetTextureDebugBinding(texture, binding);
		}
	}

	void VulkanDevice::SetBufferDebugBinding(
		RHIBufferHandle buffer, const RHIResourceDebugBindingDesc& binding) noexcept
	{
		if (RequireOwnerThread("VulkanDevice::SetBufferDebugBinding"))
		{
			m_ResourceManager.SetBufferDebugBinding(buffer, binding);
		}
	}

	std::string_view VulkanDevice::GetTextureDebugName(RHITextureHandle texture) const noexcept
	{
		return RequireOwnerThread("VulkanDevice::GetTextureDebugName")
			? m_ResourceManager.GetTextureDebugName(texture)
			: std::string_view{};
	}

	std::string_view VulkanDevice::GetBufferDebugName(RHIBufferHandle buffer) const noexcept
	{
		return RequireOwnerThread("VulkanDevice::GetBufferDebugName")
			? m_ResourceManager.GetBufferDebugName(buffer)
			: std::string_view{};
	}

	void* VulkanDevice::MapBuffer(
		RHIBufferHandle buffer, RHIMappedBufferRange readRange) noexcept
	{
		return RequireOwnerThread("VulkanDevice::MapBuffer")
			? m_ResourceManager.MapBuffer(buffer, readRange)
			: nullptr;
	}

	void VulkanDevice::UnmapBuffer(
		RHIBufferHandle buffer, RHIMappedBufferRange writtenRange) noexcept
	{
		if (RequireOwnerThread("VulkanDevice::UnmapBuffer"))
		{
			m_ResourceManager.UnmapBuffer(buffer, writtenRange);
		}
	}

	uint32_t VulkanDevice::GetBufferViewAlignment(RHIBufferViewType viewType) const noexcept
	{
		VkDeviceSize alignment = 1;
		switch (viewType)
		{
		case RHIBufferViewType::ConstantBuffer:
			alignment = m_PhysicalDeviceLimits.minUniformBufferOffsetAlignment;
			break;
		case RHIBufferViewType::ShaderResource:
		case RHIBufferViewType::UnorderedAccess:
			alignment = std::max(m_PhysicalDeviceLimits.minStorageBufferOffsetAlignment,
				m_PhysicalDeviceLimits.minTexelBufferOffsetAlignment);
			break;
		}
		return static_cast<uint32_t>(std::max<VkDeviceSize>(alignment, 1));
	}

	bool VulkanDevice::IsAlive(RHITextureHandle texture) const noexcept
	{
		return RequireOwnerThread("VulkanDevice::IsAlive(texture)") &&
			m_ResourceManager.IsAlive(texture);
	}

	bool VulkanDevice::IsAlive(RHIBufferHandle buffer) const noexcept
	{
		return RequireOwnerThread("VulkanDevice::IsAlive(buffer)") &&
			m_ResourceManager.IsAlive(buffer);
	}

	bool VulkanDevice::IsAlive(RHISamplerHandle sampler) const noexcept
	{
		return RequireOwnerThread("VulkanDevice::IsAlive(sampler)") &&
			m_ResourceManager.IsSamplerAlive(sampler);
	}

	RHIDescriptorHandle VulkanDevice::GetTextureViewDescriptor(
		RHITextureViewHandle view) const noexcept
	{
		return RequireOwnerThread("VulkanDevice::GetTextureViewDescriptor")
			? m_ResourceManager.GetTextureViewDescriptor(view)
			: RHIDescriptorHandle{};
	}

	RHIDescriptorHandle VulkanDevice::GetBufferViewDescriptor(
		RHIBufferViewHandle view) const noexcept
	{
		return RequireOwnerThread("VulkanDevice::GetBufferViewDescriptor")
			? m_ResourceManager.GetBufferViewDescriptor(view)
			: RHIDescriptorHandle{};
	}

	RHIDescriptorHandle VulkanDevice::GetSamplerDescriptor(
		RHISamplerHandle sampler) const noexcept
	{
		return RequireOwnerThread("VulkanDevice::GetSamplerDescriptor")
			? m_ResourceManager.GetSamplerDescriptor(sampler)
			: RHIDescriptorHandle{};
	}

	void VulkanDevice::RecordTextureUse(
		RHITextureHandle texture, const RHIFencePoint& fencePoint) noexcept
	{
		if (RequireOwnerThread("VulkanDevice::RecordTextureUse"))
		{
			m_ResourceManager.RecordTextureUse(texture, fencePoint);
		}
	}

	void VulkanDevice::RecordBufferUse(
		RHIBufferHandle buffer, const RHIFencePoint& fencePoint) noexcept
	{
		if (RequireOwnerThread("VulkanDevice::RecordBufferUse"))
		{
			m_ResourceManager.RecordBufferUse(buffer, fencePoint);
		}
	}

	void VulkanDevice::RetireCompletedWork() noexcept
	{
		if (RequireOwnerThread("VulkanDevice::RetireCompletedWork"))
		{
			m_ResourceManager.RetireCompletedResources();
		}
	}

	bool VulkanDevice::IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept
	{
		if (!RequireOwnerThread("VulkanDevice::IsFencePointCompleted"))
		{
			return false;
		}
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
			m_DescriptorManager.Finalize();
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
