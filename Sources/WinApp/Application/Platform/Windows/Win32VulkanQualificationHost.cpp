#include "Application/Platform/Windows/Win32VulkanQualificationHost.h"

#include <algorithm>
#include <format>

namespace gglab
{
	bool Win32VulkanQualificationHost::QueryDrawableExtent(
		VulkanQualificationDrawableExtent& outExtent, std::string& outError) const noexcept
	{
		outExtent = {};
		outError.clear();
		if (m_Window == nullptr)
		{
			outError = "the Win32 window handle is null";
			return false;
		}

		RECT clientRect{};
		if (!GetClientRect(m_Window, &clientRect))
		{
			outError = std::format(
				"GetClientRect failed with error {}", static_cast<uint32_t>(GetLastError()));
			return false;
		}
		outExtent.m_Width =
			static_cast<uint32_t>(std::max<LONG>(clientRect.right - clientRect.left, 0));
		outExtent.m_Height =
			static_cast<uint32_t>(std::max<LONG>(clientRect.bottom - clientRect.top, 0));
		return true;
	}

	bool Win32VulkanQualificationHost::ResizeDrawable(
		uint32_t width, uint32_t height, std::string& outError) noexcept
	{
		outError.clear();
		if (m_Window == nullptr)
		{
			outError = "the Win32 window handle is null";
			return false;
		}
		if (!SetWindowPos(m_Window, nullptr, 0, 0, static_cast<int>(width),
			static_cast<int>(height), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
		{
			outError = std::format(
				"SetWindowPos failed with error {}", static_cast<uint32_t>(GetLastError()));
			return false;
		}
		return true;
	}

	bool Win32VulkanQualificationHost::SetMinimized(
		bool minimized, std::string& outError) noexcept
	{
		outError.clear();
		if (m_Window == nullptr)
		{
			outError = "the Win32 window handle is null";
			return false;
		}
		ShowWindow(m_Window, minimized ? SW_MINIMIZE : SW_RESTORE);
		return true;
	}
}
