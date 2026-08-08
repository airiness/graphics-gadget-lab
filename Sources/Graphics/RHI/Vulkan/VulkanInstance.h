#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <memory>
#include <string>
#include <vector>

namespace gglab
{
	// Owns the VkInstance and the optional VK_EXT_debug_utils messenger.
	// Destruction order: debug messenger first, then the instance.
	class VulkanInstance
	{
	public:
		struct CreateInfo
		{
			// Enables the Khronos validation layer and debug messenger when
			// VK_LAYER_KHRONOS_validation and VK_EXT_debug_utils are available.
			bool m_RequestValidation = false;
			// True for Debug builds; controls the diagnostic when requested
			// validation is unavailable.
			bool m_IsDebugBuild = false;
		};

		struct Result
		{
			std::unique_ptr<VulkanInstance> m_Instance;
			// True when the debug messenger is active.
			bool m_HasDebugMessenger = false;
			// Human-readable failure detail when creation failed.
			std::string m_Error;
			VkResult m_Result = VK_SUCCESS;

			[[nodiscard]] bool Succeeded() const noexcept { return m_Instance != nullptr; }
		};

	public:
		VulkanInstance() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanInstance);
		~VulkanInstance();

		[[nodiscard]] static Result Create(const CreateInfo& createInfo) noexcept;

		[[nodiscard]] VkInstance Get() const noexcept { return m_Instance; }
		[[nodiscard]] VkDebugUtilsMessengerEXT GetDebugMessenger() const noexcept
		{
			return m_DebugMessenger;
		}
		[[nodiscard]] bool HasDebugMessenger() const noexcept
		{
			return m_DebugMessenger != VK_NULL_HANDLE;
		}

	private:
		void Destroy() noexcept;

		VkInstance m_Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
		PFN_vkDestroyDebugUtilsMessengerEXT m_DestroyDebugMessenger = nullptr;
	};
}
