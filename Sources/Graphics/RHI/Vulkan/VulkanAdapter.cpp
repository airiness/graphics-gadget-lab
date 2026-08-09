#include "Core/Precompiled.h"
#include "Graphics/RHI/Vulkan/VulkanAdapter.h"
#include "Graphics/RHI/Vulkan/VulkanShaderBindingABI.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace gglab
{
	namespace
	{
		constexpr std::string_view SwapchainExtensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
		constexpr std::string_view MutableDescriptorTypeExtensionName =
			VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME;

		// Format requirements of the current production pipeline. GTAO has a
		// preferred R8_UNORM and a fallback R16_SFLOAT candidate; either one
		// satisfies the gate, matching the production fallback contract.
		struct VulkanRequiredFormat
		{
			VkFormat m_Format;
			VkFormatFeatureFlags2 m_RequiredFeatures;
			std::string_view m_Usage;
		};
		inline constexpr std::array RequiredFormats{
			VulkanRequiredFormat{
				VK_FORMAT_D32_SFLOAT,
				VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT |
					VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT,
				"sampleable depth and shadow map",
			},
			VulkanRequiredFormat{
				VK_FORMAT_R16G16B16A16_SFLOAT,
				VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
					VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT,
				"HDR scene color and bloom",
			},
			VulkanRequiredFormat{
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
					VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT,
				"preview and auxiliary targets",
			},
			VulkanRequiredFormat{
				VK_FORMAT_B8G8R8A8_UNORM,
				VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT,
				"swapchain backbuffer",
			},
			VulkanRequiredFormat{
				VK_FORMAT_R8_UNORM,
				VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT |
					VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT,
				"GTAO AO (preferred)",
			},
			VulkanRequiredFormat{
				VK_FORMAT_R16_SFLOAT,
				VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT |
					VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT,
				"GTAO AO (fallback)",
			},
		};

		constexpr uint32_t GtaoPreferredIndex = 4;
		constexpr uint32_t GtaoFallbackIndex = 5;

		[[nodiscard]] std::string FormatUuid(const std::array<uint8_t, VK_UUID_SIZE>& uuid) noexcept
		{
			std::string text;
			text.reserve(VK_UUID_SIZE * 2);
			for (const uint8_t byte : uuid)
			{
				text += std::format("{:02x}", byte);
			}
			return text;
		}

		[[nodiscard]] VkPhysicalDeviceProperties2 QueryDeviceProperties(
			VkPhysicalDevice physicalDevice, VkPhysicalDeviceIDProperties& idProperties,
			VkPhysicalDeviceDriverProperties& driverProperties,
			VkPhysicalDeviceDescriptorIndexingProperties& descriptorIndexingProperties) noexcept
		{
			VkPhysicalDeviceProperties2 properties2{};
			properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

			descriptorIndexingProperties.sType =
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
			properties2.pNext = &descriptorIndexingProperties;

			idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
			descriptorIndexingProperties.pNext = &idProperties;

			driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
			idProperties.pNext = &driverProperties;

			vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
			return properties2;
		}

		[[nodiscard]] VkPhysicalDeviceFeatures2 QueryDeviceFeatures(VkPhysicalDevice physicalDevice,
			VkPhysicalDeviceVulkan13Features& vulkan13Features,
			VkPhysicalDeviceVulkan12Features& vulkan12Features,
			VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT& mutableDescriptorFeatures,
			VkPhysicalDeviceCustomBorderColorFeaturesEXT& customBorderColorFeatures,
			VkPhysicalDeviceImageViewMinLodFeaturesEXT& imageViewMinLodFeatures,
			VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT& vertexAttributeDivisorFeatures,
			bool hasCustomBorderColor, bool hasImageViewMinLod,
			bool hasVertexAttributeDivisor) noexcept
		{
			VkPhysicalDeviceFeatures2 features2{};
			features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

			vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
			features2.pNext = &vulkan13Features;

			// VkPhysicalDeviceVulkan12Features carries the descriptor-indexing
			// fields; a separate VkPhysicalDeviceDescriptorIndexingFeatures
			// structure is illegal in the same pNext chain.
			vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
			vulkan13Features.pNext = &vulkan12Features;

			mutableDescriptorFeatures.sType =
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT;
			vulkan12Features.pNext = &mutableDescriptorFeatures;

			VkBaseOutStructure* tail =
				reinterpret_cast<VkBaseOutStructure*>(&mutableDescriptorFeatures);
			if (hasCustomBorderColor)
			{
				customBorderColorFeatures.sType =
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
				tail->pNext = reinterpret_cast<VkBaseOutStructure*>(&customBorderColorFeatures);
				tail = reinterpret_cast<VkBaseOutStructure*>(&customBorderColorFeatures);
			}
			if (hasImageViewMinLod)
			{
				imageViewMinLodFeatures.sType =
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT;
				tail->pNext = reinterpret_cast<VkBaseOutStructure*>(&imageViewMinLodFeatures);
				tail = reinterpret_cast<VkBaseOutStructure*>(&imageViewMinLodFeatures);
			}
			if (hasVertexAttributeDivisor)
			{
				vertexAttributeDivisorFeatures.sType =
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT;
				tail->pNext =
					reinterpret_cast<VkBaseOutStructure*>(&vertexAttributeDivisorFeatures);
			}

			vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
			return features2;
		}

		[[nodiscard]] bool QueryQueueFamilySupport(VkPhysicalDevice physicalDevice,
			VkSurfaceKHR surface, uint32_t& outFamilyIndex, uint32_t& outQueueCount) noexcept
		{
			uint32_t familyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
			std::vector<VkQueueFamilyProperties> families(familyCount);
			if (familyCount > 0)
			{
				vkGetPhysicalDeviceQueueFamilyProperties(
					physicalDevice, &familyCount, families.data());
			}

			for (uint32_t index = 0; index < families.size(); ++index)
			{
				const bool graphics = (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
				VkBool32 present = VK_FALSE;
				const VkResult supportResult =
					vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, index, surface, &present);
				if (graphics && supportResult == VK_SUCCESS && present == VK_TRUE)
				{
					outFamilyIndex = index;
					outQueueCount = families[index].queueCount;
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] std::vector<VulkanFormatSupportDiagnostic> QueryFormatSupport(
			VkPhysicalDevice physicalDevice) noexcept
		{
			std::vector<VulkanFormatSupportDiagnostic> diagnostics;
			diagnostics.reserve(RequiredFormats.size());
			for (const VulkanRequiredFormat& requirement : RequiredFormats)
			{
				VkFormatProperties2 formatProperties{};
				formatProperties.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
				VkFormatProperties3 formatProperties3{};
				formatProperties3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
				formatProperties.pNext = &formatProperties3;
				vkGetPhysicalDeviceFormatProperties2(
					physicalDevice, requirement.m_Format, &formatProperties);

				const VkFormatFeatureFlags2 supported = formatProperties3.optimalTilingFeatures;
				const bool supportedFlags =
					(supported & requirement.m_RequiredFeatures) == requirement.m_RequiredFeatures;
				diagnostics.push_back({
					.m_FormatName = std::format("VK_FORMAT_{:d}", static_cast<int>(requirement.m_Format)),
					.m_Format = requirement.m_Format,
					.m_Supported = supportedFlags,
					.m_Usage = std::string(requirement.m_Usage),
					});
			}
			return diagnostics;
		}

		[[nodiscard]] bool IsGtaoCandidateSupported(
			const std::vector<VulkanFormatSupportDiagnostic>& diagnostics) noexcept
		{
			const bool preferred = GtaoPreferredIndex < diagnostics.size() &&
				diagnostics[GtaoPreferredIndex].m_Supported;
			const bool fallback = GtaoFallbackIndex < diagnostics.size() &&
				diagnostics[GtaoFallbackIndex].m_Supported;
			return preferred || fallback;
		}

		[[nodiscard]] bool AreAllRequiredFormatsSupported(
			const std::vector<VulkanFormatSupportDiagnostic>& diagnostics) noexcept
		{
			for (size_t index = 0; index < diagnostics.size(); ++index)
			{
				if (index == GtaoPreferredIndex || index == GtaoFallbackIndex)
				{
					continue;
				}
				if (!diagnostics[index].m_Supported)
				{
					return false;
				}
			}
			return IsGtaoCandidateSupported(diagnostics);
		}
	}

	std::string VulkanAdapterIdentity::UuidHex() const noexcept
	{
		return FormatUuid(m_DeviceUuid);
	}

	VulkanAdapterCapabilitySnapshot QueryVulkanAdapterCapabilitySnapshot(VkInstance instance,
		VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t enumerationIndex) noexcept
	{
		GGLAB_UNUSED(instance);
		VulkanAdapterCapabilitySnapshot snapshot{};
		snapshot.m_Identity.m_EnumerationIndex = enumerationIndex;

		VkPhysicalDeviceIDProperties idProperties{};
		VkPhysicalDeviceDriverProperties driverProperties{};
		VkPhysicalDeviceDescriptorIndexingProperties descriptorIndexingProperties{};
		const VkPhysicalDeviceProperties2 properties2 = QueryDeviceProperties(
			physicalDevice, idProperties, driverProperties, descriptorIndexingProperties);
		const VkPhysicalDeviceProperties& properties = properties2.properties;

		snapshot.m_Identity.m_DeviceName = properties.deviceName;
		snapshot.m_Identity.m_DeviceType = properties.deviceType;
		snapshot.m_Identity.m_VendorId = properties.vendorID;
		snapshot.m_Identity.m_DeviceId = properties.deviceID;
		snapshot.m_Identity.m_ApiVersion = properties.apiVersion;
		snapshot.m_Identity.m_DriverVersion = properties.driverVersion;
		snapshot.m_Identity.m_DriverName = driverProperties.driverName;
		snapshot.m_Identity.m_DriverInfo = driverProperties.driverInfo;
		std::memcpy(snapshot.m_Identity.m_DeviceUuid.data(), idProperties.deviceUUID,
			VK_UUID_SIZE);
		std::memcpy(snapshot.m_Identity.m_DriverUuid.data(), idProperties.driverUUID,
			VK_UUID_SIZE);

		snapshot.m_ProfileCapabilities.m_ApiVersion = {
			.m_Major = VkApiVersionMajor(properties.apiVersion),
			.m_Minor = VkApiVersionMinor(properties.apiVersion),
		};

		snapshot.m_HasGraphicsPresentQueueFamily = QueryQueueFamilySupport(physicalDevice, surface,
			snapshot.m_GraphicsPresentQueueFamilyIndex, snapshot.m_GraphicsPresentQueueCount);
		snapshot.m_ProfileCapabilities.m_HasGraphicsPresentQueue =
			snapshot.m_HasGraphicsPresentQueueFamily;

		uint32_t extensionCount = 0;
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> deviceExtensions(extensionCount);
		if (extensionCount > 0)
		{
			vkEnumerateDeviceExtensionProperties(
				physicalDevice, nullptr, &extensionCount, deviceExtensions.data());
		}
		snapshot.m_AvailableDeviceExtensions.reserve(deviceExtensions.size());
		for (const VkExtensionProperties& extension : deviceExtensions)
		{
			snapshot.m_AvailableDeviceExtensions.emplace_back(extension.extensionName);
		}

		const bool hasSwapchain = ContainsExtension(deviceExtensions, SwapchainExtensionName);
		const bool hasMutableDescriptorType =
			ContainsExtension(deviceExtensions, MutableDescriptorTypeExtensionName);
		constexpr std::string_view CustomBorderColorExtensionName =
			VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME;
		constexpr std::string_view ImageViewMinLodExtensionName =
			VK_EXT_IMAGE_VIEW_MIN_LOD_EXTENSION_NAME;
		constexpr std::string_view VertexAttributeDivisorExtensionName =
			VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME;
		const bool hasCustomBorderColor =
			ContainsExtension(deviceExtensions, CustomBorderColorExtensionName);
		const bool hasImageViewMinLod =
			ContainsExtension(deviceExtensions, ImageViewMinLodExtensionName);
		const bool hasVertexAttributeDivisor =
			ContainsExtension(deviceExtensions, VertexAttributeDivisorExtensionName);
		snapshot.m_ProfileCapabilities.m_HasSwapchainExtension = hasSwapchain;
		snapshot.m_ProfileCapabilities.m_HasMutableDescriptorTypeExtension =
			hasMutableDescriptorType;
		if (!hasSwapchain)
		{
			snapshot.m_MissingRequiredDeviceExtensions.emplace_back(SwapchainExtensionName);
		}
		if (!hasMutableDescriptorType)
		{
			snapshot.m_MissingRequiredDeviceExtensions.emplace_back(MutableDescriptorTypeExtensionName);
		}

		VkPhysicalDeviceVulkan13Features vulkan13Features{};
		VkPhysicalDeviceVulkan12Features vulkan12Features{};
		VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableDescriptorFeatures{};
		VkPhysicalDeviceCustomBorderColorFeaturesEXT customBorderColorFeatures{};
		VkPhysicalDeviceImageViewMinLodFeaturesEXT imageViewMinLodFeatures{};
		VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT vertexAttributeDivisorFeatures{};
		const VkPhysicalDeviceFeatures2 features2 = QueryDeviceFeatures(
			physicalDevice, vulkan13Features, vulkan12Features, mutableDescriptorFeatures,
			customBorderColorFeatures, imageViewMinLodFeatures, vertexAttributeDivisorFeatures,
			hasCustomBorderColor, hasImageViewMinLod, hasVertexAttributeDivisor);

		auto& capabilities = snapshot.m_ProfileCapabilities;
		capabilities.m_DynamicRendering = vulkan13Features.dynamicRendering == VK_TRUE;
		capabilities.m_Synchronization2 = vulkan13Features.synchronization2 == VK_TRUE;
		capabilities.m_TimelineSemaphore = vulkan12Features.timelineSemaphore == VK_TRUE;
		capabilities.m_ScalarBlockLayout = vulkan12Features.scalarBlockLayout == VK_TRUE;
		capabilities.m_SamplerAnisotropy = features2.features.samplerAnisotropy == VK_TRUE;
		capabilities.m_ShaderStorageImageExtendedFormats =
			features2.features.shaderStorageImageExtendedFormats == VK_TRUE;
		capabilities.m_RuntimeDescriptorArray =
			vulkan12Features.runtimeDescriptorArray == VK_TRUE;
		capabilities.m_DescriptorBindingPartiallyBound =
			vulkan12Features.descriptorBindingPartiallyBound == VK_TRUE;
		capabilities.m_DescriptorBindingUpdateUnusedWhilePending =
			vulkan12Features.descriptorBindingUpdateUnusedWhilePending == VK_TRUE;
		capabilities.m_DescriptorBindingSampledImageUpdateAfterBind =
			vulkan12Features.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE;
		capabilities.m_DescriptorBindingStorageImageUpdateAfterBind =
			vulkan12Features.descriptorBindingStorageImageUpdateAfterBind == VK_TRUE;
		capabilities.m_ShaderSampledImageArrayNonUniformIndexing =
			vulkan12Features.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
		capabilities.m_ShaderStorageImageArrayNonUniformIndexing =
			vulkan12Features.shaderStorageImageArrayNonUniformIndexing == VK_TRUE;
		capabilities.m_MutableDescriptorType = mutableDescriptorFeatures.mutableDescriptorType == VK_TRUE;

		// Capabilities recorded for diagnostics only; they are not part of
		// binding ABI revision 1 and must not gate the adapter.
		capabilities.m_DescriptorBindingVariableDescriptorCount =
			vulkan12Features.descriptorBindingVariableDescriptorCount == VK_TRUE;
		capabilities.m_DescriptorBindingUniformBufferUpdateAfterBind =
			vulkan12Features.descriptorBindingUniformBufferUpdateAfterBind == VK_TRUE;
		capabilities.m_DescriptorBindingStorageBufferUpdateAfterBind =
			vulkan12Features.descriptorBindingStorageBufferUpdateAfterBind == VK_TRUE;
		capabilities.m_ShaderUniformBufferArrayNonUniformIndexing =
			vulkan12Features.shaderUniformBufferArrayNonUniformIndexing == VK_TRUE;
		capabilities.m_ShaderStorageBufferArrayNonUniformIndexing =
			vulkan12Features.shaderStorageBufferArrayNonUniformIndexing == VK_TRUE;

		// Conditional portability capabilities. Extension-gated
		// features are only read when the extension is present, so an
		// unavailable extension never reports a stale device value.
		auto& portability = snapshot.m_PortabilityCapabilities;
		portability.m_FillModeNonSolid = features2.features.fillModeNonSolid == VK_TRUE;
		portability.m_DepthClamp = features2.features.depthClamp == VK_TRUE;
		portability.m_DepthBiasClamp = features2.features.depthBiasClamp == VK_TRUE;
		portability.m_IndependentBlend = features2.features.independentBlend == VK_TRUE;
		portability.m_CustomBorderColor = hasCustomBorderColor &&
			customBorderColorFeatures.customBorderColors == VK_TRUE;
		portability.m_ImageViewMinLod = hasImageViewMinLod &&
			imageViewMinLodFeatures.minLod == VK_TRUE;
		portability.m_VertexAttributeDivisor = hasVertexAttributeDivisor &&
			vertexAttributeDivisorFeatures.vertexAttributeInstanceRateDivisor == VK_TRUE;
		// Cube-array image views are an optional core capability tracked
		// outside the hard profile requirements.
		snapshot.m_ImageCubeArrayAvailable = features2.features.imageCubeArray == VK_TRUE;
		// Mirror-once sampling needs the mirror-clamp-to-edge address mode;
		// the feature is enabled on the device when available.
		snapshot.m_SamplerMirrorClampToEdgeAvailable =
			vulkan12Features.samplerMirrorClampToEdge == VK_TRUE;

		VulkanDescriptorCapacityLimits& limits = snapshot.m_DescriptorLimits;
		limits.m_MaxDescriptorSetUpdateAfterBindSampledImages =
			descriptorIndexingProperties.maxDescriptorSetUpdateAfterBindSampledImages;
		limits.m_MaxPerStageDescriptorUpdateAfterBindSampledImages =
			descriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindSampledImages;
		limits.m_MaxDescriptorSetUpdateAfterBindStorageImages =
			descriptorIndexingProperties.maxDescriptorSetUpdateAfterBindStorageImages;
		limits.m_MaxPerStageDescriptorUpdateAfterBindStorageImages =
			descriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindStorageImages;
		limits.m_MaxDescriptorSetUpdateAfterBindSamplers =
			descriptorIndexingProperties.maxDescriptorSetUpdateAfterBindSamplers;
		limits.m_MaxPerStageDescriptorUpdateAfterBindSamplers =
			descriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindSamplers;
		limits.m_MaxPerStageUpdateAfterBindResources =
			descriptorIndexingProperties.maxPerStageUpdateAfterBindResources;
		limits.m_MaxUpdateAfterBindDescriptorsInAllPools =
			descriptorIndexingProperties.maxUpdateAfterBindDescriptorsInAllPools;
		capabilities.m_DescriptorCapacityLimits = limits;
		snapshot.m_DescriptorCapacityAvailability =
			CalculateVulkanDescriptorCapacityAvailability(limits);

		snapshot.m_FormatDiagnostics = QueryFormatSupport(physicalDevice);
		capabilities.m_RequiredFormatFeaturesSupported =
			AreAllRequiredFormatsSupported(snapshot.m_FormatDiagnostics);

		// The layout probe requires a device and is completed by the bootstrap
		// before the final profile evaluation.
		capabilities.m_GlobalDescriptorSetLayoutSupported = false;

		return snapshot;
	}

	bool ProbeGlobalDescriptorSetLayoutSupport(VkDevice device) noexcept
	{
		const uint32_t resourceCount =
			GGLabVulkanShaderBindingABI.m_DescriptorCapacity.m_ResourceDescriptorCount;
		const uint32_t samplerCount =
			GGLabVulkanShaderBindingABI.m_DescriptorCapacity.m_SamplerDescriptorCount;
		const uint32_t globalSet = GGLabVulkanShaderBindingABI.m_GlobalDescriptorSet;
		GGLAB_UNUSED(globalSet);

		constexpr std::array MutableDescriptorTypes{
			VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		};

		std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
		bindings[0] = {
			.binding = GGLabVulkanShaderBindingABI.m_ResourceHeapBinding,
			.descriptorType = VK_DESCRIPTOR_TYPE_MUTABLE_EXT,
			.descriptorCount = resourceCount,
			.stageFlags = VK_SHADER_STAGE_ALL,
			.pImmutableSamplers = nullptr,
		};
		bindings[1] = {
			.binding = GGLabVulkanShaderBindingABI.m_SamplerHeapBinding,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
			.descriptorCount = samplerCount,
			.stageFlags = VK_SHADER_STAGE_ALL,
			.pImmutableSamplers = nullptr,
		};

		std::array<VkMutableDescriptorTypeListEXT, 2> mutableLists{};
		mutableLists[0] = {
			.descriptorTypeCount = static_cast<uint32_t>(MutableDescriptorTypes.size()),
			.pDescriptorTypes = MutableDescriptorTypes.data(),
		};
		mutableLists[1] = {
			.descriptorTypeCount = 0,
			.pDescriptorTypes = nullptr,
		};
		VkMutableDescriptorTypeCreateInfoEXT mutableCreateInfo{};
		mutableCreateInfo.sType = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT;
		mutableCreateInfo.mutableDescriptorTypeListCount =
			static_cast<uint32_t>(mutableLists.size());
		mutableCreateInfo.pMutableDescriptorTypeLists = mutableLists.data();

		std::array<VkDescriptorBindingFlags, 2> bindingFlags{
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
				VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
				VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
		};
		VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
		bindingFlagsInfo.sType =
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
		bindingFlagsInfo.pBindingFlags = bindingFlags.data();

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();
		layoutInfo.pNext = &bindingFlagsInfo;
		bindingFlagsInfo.pNext = &mutableCreateInfo;

		VkDescriptorSetLayoutSupport support{};
		support.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT;
		vkGetDescriptorSetLayoutSupport(device, &layoutInfo, &support);
		return support.supported == VK_TRUE;
	}
}
