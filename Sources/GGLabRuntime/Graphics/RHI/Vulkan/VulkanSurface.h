#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"

#include <vulkan/vulkan.h>

#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gglab
{
	class VulkanSurface final
	{
	public:
		VulkanSurface(VkInstance instance, VkSurfaceKHR surface) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanSurface);
		~VulkanSurface();

		[[nodiscard]] VkSurfaceKHR Get() const noexcept { return m_Surface; }

	private:
		void Destroy() noexcept;

		VkInstance m_Instance = VK_NULL_HANDLE;
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
	};

	class VulkanSurfaceFactoryBase
	{
	public:
		struct Result
		{
			std::unique_ptr<VulkanSurface> m_Surface;
			std::string m_Error;
			VkResult m_Result = VK_SUCCESS;

			[[nodiscard]] bool Succeeded() const noexcept { return m_Surface != nullptr; }
		};

		virtual ~VulkanSurfaceFactoryBase() = default;

		[[nodiscard]] virtual std::span<const std::string_view>
			RequiredInstanceExtensionNames() const noexcept = 0;
		[[nodiscard]] virtual Result Create(VkInstance instance) const noexcept = 0;
	};

	// Creates the surface factory selected by the current platform build. The
	// caller owns the factory and must keep it alive through instance/surface
	// bootstrap. Native handle interpretation remains in the platform leaf.
	[[nodiscard]] std::unique_ptr<VulkanSurfaceFactoryBase>
		CreateVulkanPlatformSurfaceFactory(void* nativeWindowHandle) noexcept;
}
