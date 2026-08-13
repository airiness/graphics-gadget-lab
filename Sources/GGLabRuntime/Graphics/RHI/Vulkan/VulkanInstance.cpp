#include "Graphics/RHI/Vulkan/VulkanInstance.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"
#include "Core/Log/LogMacros.h"

#include <array>

namespace gglab
{
	namespace
	{
		VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data,
			void* userData) noexcept
		{
			GGLAB_UNUSED(userData);
			const std::string_view severityText = ToString(severity);
			const std::string_view typeText = [types]() -> std::string_view
				{
					switch (types)
					{
					case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
						return "general";
					case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
						return "validation";
					case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
						return "performance";
					default:
						return "mixed";
					}
				}();
			const std::string_view messageIdName =
				data->pMessageIdName ? std::string_view(data->pMessageIdName) : std::string_view{};
			const std::string_view message =
				data->pMessage ? std::string_view(data->pMessage) : std::string_view{};
			const std::string text = std::format("Vulkan validation [{}] [{}] [{} #{}] {}",
				severityText, typeText,
				messageIdName.empty() ? std::string_view("<unnamed>") : messageIdName,
				data->messageIdNumber, message);

			// Messages keep their severity: validation errors/warnings are
			// investigation items, loader and info messages stay informational.
			const spdlog::level::level_enum level =
				severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
				? spdlog::level::err
				: severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
				? spdlog::level::warn
				: spdlog::level::info;
			GGLAB_LOG_GRAPHICS_ALWAYS(level, "{}", text);
			return VK_FALSE;
		}
	}

	VulkanInstance::~VulkanInstance()
	{
		Destroy();
	}

	VulkanInstance::Result VulkanInstance::Create(const CreateInfo& createInfo) noexcept
	{
		Result result{};

		uint32_t instanceLayerCount = 0;
		VkResult enumerateResult =
			vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr);
		if (enumerateResult != VK_SUCCESS)
		{
			result.m_Result = enumerateResult;
			result.m_Error = std::format("vkEnumerateInstanceLayerProperties failed with {}.",
				ToString(enumerateResult));
			return result;
		}
		std::vector<VkLayerProperties> layers(instanceLayerCount);
		if (instanceLayerCount > 0)
		{
			enumerateResult = vkEnumerateInstanceLayerProperties(&instanceLayerCount, layers.data());
			if (enumerateResult != VK_SUCCESS)
			{
				result.m_Result = enumerateResult;
				result.m_Error = std::format("vkEnumerateInstanceLayerProperties failed with {}.",
					ToString(enumerateResult));
				return result;
			}
		}

		uint32_t extensionCount = 0;
		enumerateResult = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
		if (enumerateResult != VK_SUCCESS)
		{
			result.m_Result = enumerateResult;
			result.m_Error = std::format("vkEnumerateInstanceExtensionProperties failed with {}.",
				ToString(enumerateResult));
			return result;
		}
		std::vector<VkExtensionProperties> extensions(extensionCount);
		if (extensionCount > 0)
		{
			enumerateResult =
				vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
			if (enumerateResult != VK_SUCCESS)
			{
				result.m_Result = enumerateResult;
				result.m_Error = std::format("vkEnumerateInstanceExtensionProperties failed with {}.",
					ToString(enumerateResult));
				return result;
			}
		}

		constexpr std::string_view DebugUtilsExtensionName = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		constexpr std::string_view ValidationLayerName = "VK_LAYER_KHRONOS_validation";

		std::vector<std::string> requiredExtensionStorage;
		requiredExtensionStorage.reserve(createInfo.m_RequiredInstanceExtensions.size());
		std::vector<std::string_view> missingRequiredExtensions;
		for (const std::string_view required : createInfo.m_RequiredInstanceExtensions)
		{
			if (ContainsExtension(extensions, required))
			{
				requiredExtensionStorage.emplace_back(required);
			}
			else
			{
				missingRequiredExtensions.push_back(required);
			}
		}
		if (!missingRequiredExtensions.empty())
		{
			result.m_Result = VK_ERROR_EXTENSION_NOT_PRESENT;
			std::string missing;
			for (const std::string_view name : missingRequiredExtensions)
			{
				if (!missing.empty())
				{
					missing += " ";
				}
				missing += name;
			}
			result.m_Error =
				std::format("Required Vulkan instance extensions are unavailable: {}.", missing);
			return result;
		}

		const bool hasDebugUtils = ContainsExtension(extensions, DebugUtilsExtensionName);
		const bool hasValidationLayer = ContainsLayer(layers, ValidationLayerName);
		const bool enableValidation = createInfo.m_RequestValidation && hasDebugUtils &&
			hasValidationLayer;
		if (createInfo.m_RequestValidation && (!hasDebugUtils || !hasValidationLayer))
		{
			GGLAB_LOG_GRAPHICS_WARN_ALWAYS(
				"Vulkan validation requested but unavailable (debug_utils={}, validation layer={}).",
				hasDebugUtils, hasValidationLayer);
		}

		std::vector<const char*> enabledExtensions;
		enabledExtensions.reserve(requiredExtensionStorage.size() + 1);
		for (const std::string& required : requiredExtensionStorage)
		{
			enabledExtensions.push_back(required.c_str());
		}
		std::vector<const char*> enabledLayers;
		if (hasDebugUtils)
		{
			// Debug utils is enabled unconditionally so GGLab debug names
			// can be applied to Vulkan objects in every build. The debug
			// messenger itself is only created when validation is requested.
			enabledExtensions.push_back(DebugUtilsExtensionName.data());
		}
		if (enableValidation)
		{
			enabledLayers.push_back(ValidationLayerName.data());
		}

		VkApplicationInfo applicationInfo{};
		applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		applicationInfo.pApplicationName = "GraphicsGadgetLab";
		applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
		applicationInfo.pEngineName = "GGLab";
		applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
		applicationInfo.apiVersion = VK_API_VERSION_1_3;

		VkInstanceCreateInfo instanceCreateInfo{};
		instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instanceCreateInfo.pApplicationInfo = &applicationInfo;
		instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
		instanceCreateInfo.ppEnabledLayerNames = enabledLayers.data();
		instanceCreateInfo.enabledExtensionCount =
			static_cast<uint32_t>(enabledExtensions.size());
		instanceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();

		auto instance = std::make_unique<VulkanInstance>();
		const VkResult createResult = vkCreateInstance(&instanceCreateInfo, nullptr, &instance->m_Instance);
		if (createResult != VK_SUCCESS)
		{
			result.m_Result = createResult;
			result.m_Error = std::format(
				"vkCreateInstance failed with {} (validation requested={}).", ToString(createResult),
				createInfo.m_RequestValidation);
			return result;
		}

		if (enableValidation)
		{
			const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
				vkGetInstanceProcAddr(instance->m_Instance, "vkCreateDebugUtilsMessengerEXT"));
			instance->m_DestroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
				vkGetInstanceProcAddr(instance->m_Instance, "vkDestroyDebugUtilsMessengerEXT"));
			if (createMessenger && instance->m_DestroyDebugMessenger)
			{
				VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo{};
				messengerCreateInfo.sType =
					VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
				messengerCreateInfo.messageSeverity =
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
				messengerCreateInfo.messageType =
					VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
				messengerCreateInfo.pfnUserCallback = DebugMessengerCallback;
				const VkResult messengerResult = createMessenger(instance->m_Instance,
					&messengerCreateInfo, nullptr, &instance->m_DebugMessenger);
				if (messengerResult != VK_SUCCESS)
				{
					GGLAB_LOG_GRAPHICS_WARN_ALWAYS(
						"Failed to create the Vulkan debug messenger: {}.", ToString(messengerResult));
				}
			}
		}

		result.m_Instance = std::move(instance);
		result.m_HasDebugMessenger = result.m_Instance->HasDebugMessenger();
		return result;
	}

	void VulkanInstance::Destroy() noexcept
	{
		if (m_DebugMessenger != VK_NULL_HANDLE && m_DestroyDebugMessenger)
		{
			m_DestroyDebugMessenger(m_Instance, m_DebugMessenger, nullptr);
			m_DebugMessenger = VK_NULL_HANDLE;
		}
		m_DestroyDebugMessenger = nullptr;
		if (m_Instance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(m_Instance, nullptr);
			m_Instance = VK_NULL_HANDLE;
		}
	}
}
