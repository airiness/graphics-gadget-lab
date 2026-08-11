#pragma once
#include "Application/Platform/PlatformHost.h"
#include "Application/Platform/PlatformWindow.h"
#include "Core/CoreMacros.h"

#include <windows.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace gglab
{
	class Win32MessageHandler;
	struct Win32MessageResult;

	class Win32Window final : public PlatformWindow
	{
	public:
		class MessageSubscription
		{
		public:
			MessageSubscription() noexcept = default;
			GGLAB_DELETE_COPYABLE(MessageSubscription);
			MessageSubscription(MessageSubscription&& other) noexcept;
			MessageSubscription& operator=(MessageSubscription&& other) noexcept;
			~MessageSubscription() noexcept;

			void Reset() noexcept;
			[[nodiscard]] bool IsValid() const noexcept { return m_Window != nullptr; }

		private:
			MessageSubscription(Win32Window* window, uint64_t id) noexcept :
				m_Window(window),
				m_Id(id)
			{}

			friend class Win32Window;

			Win32Window* m_Window = nullptr;
			uint64_t m_Id = 0;
		};

	public:
		explicit Win32Window(HINSTANCE instance) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Win32Window);
		~Win32Window() override = default;

		[[nodiscard]] bool Initialize(const PlatformWindowCreateInfo& createInfo) noexcept;
		void Finalize() noexcept;

		[[nodiscard]] MessageSubscription Subscribe(Win32MessageHandler& handler) noexcept;
		[[nodiscard]] bool PollEvent(PlatformEvent& event) noexcept;

		[[nodiscard]] void* GetNativeHandle() const noexcept override { return m_Hwnd; }
		[[nodiscard]] uint32_t GetWidth() const noexcept override { return m_Width; }
		[[nodiscard]] uint32_t GetHeight() const noexcept override { return m_Height; }

	private:
		struct MessageHandlerEntry
		{
			uint64_t m_Id = 0;
			Win32MessageHandler* m_Handler = nullptr;
		};

		static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
		LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
		[[nodiscard]] Win32MessageResult DispatchMessageHandlers(
			HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

		void Unsubscribe(uint64_t id) noexcept;
		void PushEvent(PlatformEventType type, uint32_t width = 0, uint32_t height = 0) noexcept;

	private:
		HINSTANCE m_Instance = nullptr;
		HWND m_Hwnd = nullptr;
		std::wstring m_ClassName;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint64_t m_NextMessageHandlerId = 1;
		std::vector<MessageHandlerEntry> m_MessageHandlers;
		std::deque<PlatformEvent> m_Events;
		bool m_IsMinimized = false;
		bool m_InSizeMove = false;
	};
}
