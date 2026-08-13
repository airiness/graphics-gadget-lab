#include "Graphics/RHI/Vulkan/VulkanSurface.h"

namespace gglab
{
	VulkanSurface::VulkanSurface(VkInstance instance, VkSurfaceKHR surface) noexcept :
		m_Instance(instance), m_Surface(surface)
	{
	}

	VulkanSurface::~VulkanSurface()
	{
		Destroy();
	}

	void VulkanSurface::Destroy() noexcept
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
