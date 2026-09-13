#pragma once
#include "VulkanQualification.h"

#include <windows.h>

#include <string>
#include <string_view>

namespace gglab
{
	// Dedicated Win32 window owner and narrow window-control implementation for
	// the standalone Vulkan qualification process.
	class Win32QualificationHost final : public VulkanQualificationHostBase
	{
	public:
		Win32QualificationHost() noexcept = default;
		Win32QualificationHost(const Win32QualificationHost&) = delete;
		Win32QualificationHost& operator=(const Win32QualificationHost&) = delete;
		~Win32QualificationHost() override;

		[[nodiscard]] bool Initialize(HINSTANCE instance, std::wstring_view title,
			uint32_t width, uint32_t height) noexcept;
		void Shutdown() noexcept;

		[[nodiscard]] HWND GetWindow() const noexcept { return m_Window; }

		[[nodiscard]] bool QueryDrawableExtent(
			VulkanQualificationDrawableExtent& outExtent,
			std::string& outError) const noexcept override;
		[[nodiscard]] bool ResizeDrawable(
			uint32_t width, uint32_t height, std::string& outError) noexcept override;
		[[nodiscard]] bool SetMinimized(
			bool minimized, std::string& outError) noexcept override;

	private:
		static LRESULT CALLBACK WindowProc(
			HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

		HINSTANCE m_Instance = nullptr;
		HWND m_Window = nullptr;
		std::wstring m_ClassName;
	};
}
