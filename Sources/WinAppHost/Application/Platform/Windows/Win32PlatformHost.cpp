#include "Application/Platform/Windows/Win32PlatformHost.h"

#include <algorithm>
#include <chrono>

namespace gglab
{
	Win32PlatformHost::Win32PlatformHost(HINSTANCE instance) noexcept :
		m_MainWindow(instance)
	{
	}

	bool Win32PlatformHost::Initialize(const PlatformWindowCreateInfo& createInfo) noexcept
	{
		if (m_IsInitialized)
		{
			return false;
		}

		m_QuitRequested = false;
		m_WakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!m_WakeEvent)
		{
			return false;
		}
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
		if (m_WakeEvent)
		{
			CloseHandle(m_WakeEvent);
			m_WakeEvent = nullptr;
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

	void Win32PlatformHost::Wake() noexcept
	{
		SetEvent(m_WakeEvent);
	}

	void Win32PlatformHost::WaitForEvents(
		std::optional<std::chrono::steady_clock::time_point> deadline) noexcept
	{
		DWORD timeout = INFINITE;
		if (deadline)
		{
			const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
				*deadline - std::chrono::steady_clock::now());
			timeout = static_cast<DWORD>(std::clamp<int64_t>(remaining.count(), 0, INFINITE - 1));
		}
		MsgWaitForMultipleObjectsEx(1, &m_WakeEvent, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
	}
}
