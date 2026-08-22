#include "Application/Platform/Windows/Win32PlatformHost.h"

#include <Shlwapi.h>

namespace gglab
{
	Win32PlatformHost::Win32PlatformHost(HINSTANCE instance) noexcept :
		m_MainWindow(instance)
	{}

	bool Win32PlatformHost::Initialize(const PlatformWindowCreateInfo& createInfo) noexcept
	{
		if (m_IsInitialized)
		{
			return false;
		}

		WCHAR executablePath[MAX_PATH]{};
		if (GetModuleFileNameW(nullptr, executablePath, MAX_PATH) > 0)
		{
			PathRemoveFileSpecW(executablePath);
			SetCurrentDirectoryW(executablePath);
		}

		m_QuitRequested = false;
		m_IsInitialized = m_MainWindow.Initialize(createInfo);
		return m_IsInitialized;
	}

	void Win32PlatformHost::Finalize() noexcept
	{
		if (m_IsInitialized)
		{
			m_MainWindow.Finalize();
			m_IsInitialized = false;
		}
	}

	void Win32PlatformHost::PumpEvents() noexcept
	{
		MSG message{};
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
		{
			if (message.message == WM_QUIT)
			{
				m_QuitRequested = true;
				continue;
			}

			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}

	bool Win32PlatformHost::PollEvent(PlatformEvent& event) noexcept
	{
		return m_MainWindow.PollEvent(event);
	}

	void Win32PlatformHost::WaitForEvents() noexcept
	{
		WaitMessage();
	}
}
