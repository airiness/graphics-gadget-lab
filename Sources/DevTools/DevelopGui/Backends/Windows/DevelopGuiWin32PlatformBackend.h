#pragma once
#include "Application/Platform/Windows/Win32MessageHandler.h"
#include "Application/Platform/Windows/Win32Window.h"
#include "DevTools/DevelopGui/DevelopGuiPlatformBackend.h"

namespace gglab
{
	class DevelopGuiWin32PlatformBackend final :
		public DevelopGuiPlatformBackend,
		public Win32MessageHandler
	{
	public:
		DevelopGuiWin32PlatformBackend() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevelopGuiWin32PlatformBackend);
		~DevelopGuiWin32PlatformBackend() override = default;

		[[nodiscard]] bool Initialize(PlatformWindow& window) noexcept override;
		void Finalize() noexcept override;
		void NewFrame() noexcept override;

		[[nodiscard]] Win32MessageResult HandleWin32Message(
			HWND hwnd,
			UINT message,
			WPARAM wParam,
			LPARAM lParam) noexcept override;

	private:
		Win32Window::MessageSubscription m_MessageSubscription;
		bool m_IsInitialized = false;
	};
}
