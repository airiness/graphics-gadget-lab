#include "Core/Precompiled.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace gglab
{
	std::string_view ToString(VkResult result) noexcept
	{
		switch (result)
		{
		case VK_SUCCESS:
			return "VK_SUCCESS";
		case VK_NOT_READY:
			return "VK_NOT_READY";
		case VK_TIMEOUT:
			return "VK_TIMEOUT";
		case VK_EVENT_SET:
			return "VK_EVENT_SET";
		case VK_EVENT_RESET:
			return "VK_EVENT_RESET";
		case VK_INCOMPLETE:
			return "VK_INCOMPLETE";
		case VK_ERROR_OUT_OF_HOST_MEMORY:
			return "VK_ERROR_OUT_OF_HOST_MEMORY";
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:
			return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		case VK_ERROR_INITIALIZATION_FAILED:
			return "VK_ERROR_INITIALIZATION_FAILED";
		case VK_ERROR_DEVICE_LOST:
			return "VK_ERROR_DEVICE_LOST";
		case VK_ERROR_MEMORY_MAP_FAILED:
			return "VK_ERROR_MEMORY_MAP_FAILED";
		case VK_ERROR_LAYER_NOT_PRESENT:
			return "VK_ERROR_LAYER_NOT_PRESENT";
		case VK_ERROR_EXTENSION_NOT_PRESENT:
			return "VK_ERROR_EXTENSION_NOT_PRESENT";
		case VK_ERROR_FEATURE_NOT_PRESENT:
			return "VK_ERROR_FEATURE_NOT_PRESENT";
		case VK_ERROR_INCOMPATIBLE_DRIVER:
			return "VK_ERROR_INCOMPATIBLE_DRIVER";
		case VK_ERROR_TOO_MANY_OBJECTS:
			return "VK_ERROR_TOO_MANY_OBJECTS";
		case VK_ERROR_FORMAT_NOT_SUPPORTED:
			return "VK_ERROR_FORMAT_NOT_SUPPORTED";
		case VK_ERROR_FRAGMENTED_POOL:
			return "VK_ERROR_FRAGMENTED_POOL";
		case VK_ERROR_UNKNOWN:
			return "VK_ERROR_UNKNOWN";
		case VK_ERROR_OUT_OF_POOL_MEMORY:
			return "VK_ERROR_OUT_OF_POOL_MEMORY";
		case VK_ERROR_INVALID_EXTERNAL_HANDLE:
			return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
		case VK_ERROR_FRAGMENTATION:
			return "VK_ERROR_FRAGMENTATION";
		case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
			return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
		case VK_ERROR_SURFACE_LOST_KHR:
			return "VK_ERROR_SURFACE_LOST_KHR";
		case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
			return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
		case VK_SUBOPTIMAL_KHR:
			return "VK_SUBOPTIMAL_KHR";
		case VK_ERROR_OUT_OF_DATE_KHR:
			return "VK_ERROR_OUT_OF_DATE_KHR";
		case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
			return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
		case VK_ERROR_VALIDATION_FAILED_EXT:
			return "VK_ERROR_VALIDATION_FAILED_EXT";
		case VK_ERROR_INVALID_SHADER_NV:
			return "VK_ERROR_INVALID_SHADER_NV";
		default:
			return "VK_UNKNOWN_RESULT";
		}
	}

	std::string_view ToString(VkPhysicalDeviceType type) noexcept
	{
		switch (type)
		{
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
			return "integrated";
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
			return "discrete";
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
			return "virtual";
		case VK_PHYSICAL_DEVICE_TYPE_CPU:
			return "cpu";
		case VK_PHYSICAL_DEVICE_TYPE_OTHER:
			return "other";
		default:
			return "unknown";
		}
	}

	std::string_view ToString(VkDebugUtilsMessageSeverityFlagBitsEXT severity) noexcept
	{
		switch (severity)
		{
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
			return "verbose";
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			return "info";
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			return "warning";
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			return "error";
		default:
			return "unknown";
		}
	}

	std::string FormatVkVersion(uint32_t version) noexcept
	{
		return std::format("{}.{}.{}", VkApiVersionMajor(version), VkApiVersionMinor(version),
			VkApiVersionPatch(version));
	}

	uint32_t VkApiVersionMajor(uint32_t version) noexcept
	{
		return VK_API_VERSION_MAJOR(version);
	}

	uint32_t VkApiVersionMinor(uint32_t version) noexcept
	{
		return VK_API_VERSION_MINOR(version);
	}

	uint32_t VkApiVersionPatch(uint32_t version) noexcept
	{
		return VK_API_VERSION_PATCH(version);
	}

	bool ContainsExtension(
		std::span<const VkExtensionProperties> extensions, std::string_view name) noexcept
	{
		return std::ranges::any_of(extensions,
			[name](const VkExtensionProperties& extension)
			{
				return std::string_view(extension.extensionName) == name;
			});
	}

	bool ContainsLayer(
		std::span<const VkLayerProperties> layers, std::string_view name) noexcept
	{
		return std::ranges::any_of(layers,
			[name](const VkLayerProperties& layer)
			{
				return std::string_view(layer.layerName) == name;
			});
	}

	std::string FormatAdapterVersions(
		uint32_t apiVersion, uint32_t driverVersion) noexcept
	{
		return std::format("API {} / driver 0x{:08x}", FormatVkVersion(apiVersion), driverVersion);
	}

	void SetVulkanObjectDebugName(
		VkDevice device, VkObjectType objectType, uint64_t objectHandle,
		const char* name) noexcept
	{
		if (device == VK_NULL_HANDLE || objectHandle == 0 || name == nullptr || *name == '\0')
		{
			return;
		}
		const auto setName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
			vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT"));
		if (setName == nullptr)
		{
			return;
		}
		VkDebugUtilsObjectNameInfoEXT nameInfo{};
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = objectType;
		nameInfo.objectHandle = objectHandle;
		nameInfo.pObjectName = name;
		setName(device, &nameInfo);
	}
}
