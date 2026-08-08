#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <windows.h>

#include <memory>
#include <string>

namespace gglab
{
	// Owns a VkSurfaceKHR created through VK_KHR_win32_surface. The surface
	// is used to validate graphics+present queue families; swapchain creation
	// is out of scope.
	class VulkanWin32Surface
	{
	public:
		struct Result
		{
			std::unique_ptr<VulkanWin32Surface> m_Surface;
			std::string m_Error;
			VkResult m_Result = VK_SUCCESS;

			[[nodiscard]] bool Succeeded() const noexcept { return m_Surface != nullptr; }
		};

	public:
		VulkanWin32Surface() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanWin32Surface);
		~VulkanWin32Surface();

		[[nodiscard]] static Result Create(
			VkInstance instance, HINSTANCE hInstance, HWND hwnd) noexcept;

		[[nodiscard]] VkSurfaceKHR Get() const noexcept { return m_Surface; }

	private:
		void Destroy() noexcept;

		VkInstance m_Instance = VK_NULL_HANDLE;
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
	};
}
