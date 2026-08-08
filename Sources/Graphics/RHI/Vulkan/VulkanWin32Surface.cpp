#include "Core/Precompiled.h"
#include "Graphics/RHI/Vulkan/VulkanWin32Surface.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

namespace gglab
{
	VulkanWin32Surface::~VulkanWin32Surface()
	{
		Destroy();
	}

	VulkanWin32Surface::Result VulkanWin32Surface::Create(
		VkInstance instance, HINSTANCE hInstance, HWND hwnd) noexcept
	{
		Result result{};
		if (instance == VK_NULL_HANDLE || hInstance == nullptr || hwnd == nullptr)
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			result.m_Error = "VulkanWin32Surface requires a valid instance, HINSTANCE, and HWND.";
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
