#include "DevTools/DevelopGui/Backends/Windows/DevelopGuiWin32PlatformBackend.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"

#include <backends/imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
	HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace gglab
{
	bool DevelopGuiWin32PlatformBackend::Initialize(PlatformWindow& window) noexcept
	{
		if (m_IsInitialized)
		{
			return false;
		}

		auto* win32Window = dynamic_cast<Win32Window*>(&window);
		if (!win32Window || !win32Window->GetNativeHandle())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DevelopGui Win32 backend requires an initialized Win32Window.");
			return false;
		}

		if (!ImGui_ImplWin32_Init(win32Window->GetNativeHandle()))
		{
			GGLAB_LOG_GRAPHICS_ERROR("Failed to initialize the ImGui Win32 backend.");
			return false;
		}

		m_MessageSubscription = win32Window->Subscribe(*this);
		m_IsInitialized = true;
		return true;
	}

	void DevelopGuiWin32PlatformBackend::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		// Stop native message delivery before tearing down ImGui platform state.
		m_MessageSubscription.Reset();
		ImGui_ImplWin32_Shutdown();
		m_IsInitialized = false;
	}

	void DevelopGuiWin32PlatformBackend::NewFrame() noexcept
	{
		GGLAB_ASSERT(m_IsInitialized);
		if (m_IsInitialized)
		{
			ImGui_ImplWin32_NewFrame();
		}
	}

	Win32MessageResult DevelopGuiWin32PlatformBackend::HandleWin32Message(
		HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
	{
		if (!m_IsInitialized)
		{
			return {};
		}

		const bool handled = ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
		return {
			.m_Handled = handled,
			.m_Result = handled ? 1 : 0,
		};
	}
}
