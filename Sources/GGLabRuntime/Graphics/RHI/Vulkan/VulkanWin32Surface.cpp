#include "Graphics/RHI/Vulkan/VulkanWin32Surface.h"
#include "Graphics/RHI/Vulkan/VulkanBootstrap.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <format>

namespace gglab
{
	namespace
	{
		constexpr std::string_view RequiredInstanceExtensions[] = {
			VK_KHR_SURFACE_EXTENSION_NAME,
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
		};
	}

	VulkanWin32SurfaceFactory::VulkanWin32SurfaceFactory(
		HINSTANCE instance, HWND window) noexcept :
		m_Instance(instance), m_Window(window)
	{
	}

	std::span<const std::string_view>
		VulkanWin32SurfaceFactory::RequiredInstanceExtensionNames() const noexcept
	{
		return RequiredInstanceExtensions;
	}

	VulkanSurfaceFactoryBase::Result VulkanWin32SurfaceFactory::Create(
		VkInstance instance) const noexcept
	{
		Result result{};
		if (instance == VK_NULL_HANDLE || m_Instance == nullptr || m_Window == nullptr)
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			result.m_Error = "Vulkan Win32 surface creation requires valid instance and window handles.";
			return result;
		}

		const auto createSurface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
			vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
		if (!createSurface)
		{
			result.m_Result = VK_ERROR_EXTENSION_NOT_PRESENT;
			result.m_Error =
				"vkCreateWin32SurfaceKHR is unavailable; VK_KHR_win32_surface is not enabled.";
			return result;
		}

		VkWin32SurfaceCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		createInfo.hinstance = m_Instance;
		createInfo.hwnd = m_Window;

		VkSurfaceKHR surface = VK_NULL_HANDLE;
		const VkResult createResult = createSurface(instance, &createInfo, nullptr, &surface);
		if (createResult != VK_SUCCESS)
		{
			result.m_Result = createResult;
			result.m_Error =
				std::format("vkCreateWin32SurfaceKHR failed with {}.", ToString(createResult));
			return result;
		}

		result.m_Surface = std::make_unique<VulkanSurface>(instance, surface);
		return result;
	}

	VulkanBootstrapRuntimeResult CreateVulkanBootstrapRuntimeForWindow(
		VulkanBootstrapRuntimeCreateInfo createInfo, void* nativeWindowHandle) noexcept
	{
		VulkanWin32SurfaceFactory surfaceFactory(
			GetModuleHandleW(nullptr), static_cast<HWND>(nativeWindowHandle));
		createInfo.m_BootstrapOptions.m_SurfaceFactory = &surfaceFactory;
		return CreateVulkanBootstrapRuntime(createInfo);
	}
}
