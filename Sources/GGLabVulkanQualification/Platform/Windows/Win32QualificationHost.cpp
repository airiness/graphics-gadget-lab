#include "Platform/Windows/Win32QualificationHost.h"

#include <algorithm>
#include <format>

namespace
{
	constexpr wchar_t QualificationWindowClassName[] =
		L"GGLabVulkanQualification.WindowClass";
}

namespace gglab
{
	Win32QualificationHost::~Win32QualificationHost()
	{
		Shutdown();
	}

	bool Win32QualificationHost::Initialize(HINSTANCE instance, std::wstring_view title,
		uint32_t width, uint32_t height) noexcept
	{
		if (m_Window || !instance || title.empty() || width == 0 || height == 0)
		{
			return false;
		}

		m_Instance = instance;
		m_ClassName = QualificationWindowClassName;

		WNDCLASSEXW windowClass{};
		windowClass.cbSize = sizeof(windowClass);
		windowClass.style = CS_HREDRAW | CS_VREDRAW;
		windowClass.lpfnWndProc = WindowProc;
		windowClass.hInstance = m_Instance;
		windowClass.hIcon = LoadIconW(m_Instance, IDI_APPLICATION);
		windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
		windowClass.lpszClassName = m_ClassName.c_str();
		windowClass.hIconSm = LoadIconW(m_Instance, IDI_APPLICATION);
		if (!RegisterClassExW(&windowClass))
		{
			Shutdown();
			return false;
		}

		RECT windowRect{
			0,
			0,
			static_cast<LONG>(width),
			static_cast<LONG>(height),
		};
		if (!AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE))
		{
			Shutdown();
			return false;
		}

		const std::wstring windowTitle(title);
		m_Window = CreateWindowExW(
			0,
			m_ClassName.c_str(),
			windowTitle.c_str(),
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			windowRect.right - windowRect.left,
			windowRect.bottom - windowRect.top,
			nullptr,
			nullptr,
			m_Instance,
			nullptr);
		if (!m_Window)
		{
			Shutdown();
			return false;
		}

		ShowWindow(m_Window, SW_SHOWDEFAULT);
		UpdateWindow(m_Window);
		return true;
	}

	void Win32QualificationHost::Shutdown() noexcept
	{
		if (m_Window)
		{
			DestroyWindow(m_Window);
			m_Window = nullptr;
		}
		if (m_Instance && !m_ClassName.empty())
		{
			UnregisterClassW(m_ClassName.c_str(), m_Instance);
		}
		m_ClassName.clear();
		m_Instance = nullptr;
	}

	bool Win32QualificationHost::QueryDrawableExtent(
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

	bool Win32QualificationHost::ResizeDrawable(
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

	bool Win32QualificationHost::SetMinimized(
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

	LRESULT CALLBACK Win32QualificationHost::WindowProc(
		HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
	{
		if (message == WM_PAINT)
		{
			PAINTSTRUCT paint{};
			BeginPaint(window, &paint);
			EndPaint(window, &paint);
			return 0;
		}
		return DefWindowProcW(window, message, wParam, lParam);
	}
}
