#pragma once
#include "Application/Rendering/VulkanQualification.h"

#include <windows.h>

namespace gglab
{
	// Application-owned Win32 implementation of the narrow window-control
	// contract consumed by the Vulkan qualification harness.
	class Win32VulkanQualificationHost final : public VulkanQualificationHostBase
	{
	public:
		explicit Win32VulkanQualificationHost(HWND window) noexcept : m_Window(window) {}

		[[nodiscard]] bool QueryDrawableExtent(
			VulkanQualificationDrawableExtent& outExtent,
			std::string& outError) const noexcept override;
		[[nodiscard]] bool ResizeDrawable(
			uint32_t width, uint32_t height, std::string& outError) noexcept override;
		[[nodiscard]] bool SetMinimized(
			bool minimized, std::string& outError) noexcept override;

	private:
		HWND m_Window = nullptr;
	};
}
