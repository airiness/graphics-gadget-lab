#pragma once
#include "Graphics/RHI/Vulkan/VulkanSurface.h"

#include <windows.h>

#include <vulkan/vulkan_win32.h>

namespace gglab
{
	class VulkanWin32SurfaceFactory final : public VulkanSurfaceFactoryBase
	{
	public:
		VulkanWin32SurfaceFactory(HINSTANCE instance, HWND window) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanWin32SurfaceFactory);
		~VulkanWin32SurfaceFactory() override = default;

		[[nodiscard]] std::span<const std::string_view>
			RequiredInstanceExtensionNames() const noexcept override;
		[[nodiscard]] Result Create(VkInstance instance) const noexcept override;

	private:
		HINSTANCE m_Instance = nullptr;
		HWND m_Window = nullptr;
	};
}
