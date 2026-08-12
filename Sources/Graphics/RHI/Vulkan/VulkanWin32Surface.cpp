#include "Graphics/RHI/Vulkan/VulkanWin32Surface.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <windows.h>

#include <vulkan/vulkan_win32.h>

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

	std::span<const std::string_view> VulkanWin32Surface::RequiredInstanceExtensionNames() noexcept
	{
		return RequiredInstanceExtensions;
	}

	VulkanWin32Surface::~VulkanWin32Surface()
	{
		Destroy();
	}

	VulkanWin32Surface::Result VulkanWin32Surface::Create(
		VkInstance instance, void* nativeInstanceHandle, void* nativeWindowHandle) noexcept
	{
		const HINSTANCE hInstance = static_cast<HINSTANCE>(nativeInstanceHandle);
		const HWND hwnd = static_cast<HWND>(nativeWindowHandle);

		Result result{};
		if (instance == VK_NULL_HANDLE || hInstance == nullptr || hwnd == nullptr)
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			result.m_Error =
				"VulkanWin32Surface requires a valid instance and native window handles.";
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

		auto surface = std::make_unique<VulkanWin32Surface>();
		surface->m_Instance = instance;

		VkWin32SurfaceCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		createInfo.hinstance = hInstance;
		createInfo.hwnd = hwnd;

		const VkResult createResult = createSurface(instance, &createInfo, nullptr, &surface->m_Surface);
		if (createResult != VK_SUCCESS)
		{
			result.m_Result = createResult;
			result.m_Error =
				std::format("vkCreateWin32SurfaceKHR failed with {}.", ToString(createResult));
			return result;
		}

		result.m_Surface = std::move(surface);
		return result;
	}

	void VulkanWin32Surface::Destroy() noexcept
	{
		if (m_Surface != VK_NULL_HANDLE)
		{
			const auto destroySurface = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
				vkGetInstanceProcAddr(m_Instance, "vkDestroySurfaceKHR"));
			if (destroySurface)
			{
				destroySurface(m_Instance, m_Surface, nullptr);
			}
			m_Surface = VK_NULL_HANDLE;
		}
		m_Instance = VK_NULL_HANDLE;
	}
}
