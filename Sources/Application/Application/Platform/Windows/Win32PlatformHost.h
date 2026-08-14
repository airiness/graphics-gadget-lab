#pragma once
#include "Application/Platform/PlatformHost.h"
#include "Application/Platform/Windows/Win32Window.h"
#include "GGLabFoundation/Base/CoreMacros.h"

namespace gglab
{
	class Win32PlatformHost final : public PlatformHost
	{
	public:
		explicit Win32PlatformHost(HINSTANCE instance) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Win32PlatformHost);
		~Win32PlatformHost() override = default;

		[[nodiscard]] bool Initialize(const PlatformWindowCreateInfo& createInfo) noexcept override;
		void Finalize() noexcept override;

		void PumpEvents() noexcept override;
		[[nodiscard]] bool PollEvent(PlatformEvent& event) noexcept override;
		void WaitForEvents() noexcept override;
		[[nodiscard]] bool IsQuitRequested() const noexcept override { return m_QuitRequested; }

		[[nodiscard]] PlatformWindow& GetMainWindow() noexcept override { return m_MainWindow; }

	private:
		Win32Window m_MainWindow;
		bool m_QuitRequested = false;
	};
}
