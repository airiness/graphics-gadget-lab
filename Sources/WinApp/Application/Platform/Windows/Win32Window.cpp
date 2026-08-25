#include "Application/Platform/Windows/Win32Window.h"
#include "Application/Platform/Windows/Win32MessageHandler.h"

#include <algorithm>
#include <utility>

namespace gglab
{
	Win32Window::MessageSubscription::MessageSubscription(MessageSubscription&& other) noexcept :
		m_Window(std::exchange(other.m_Window, nullptr)),
		m_Id(std::exchange(other.m_Id, 0))
	{}

	Win32Window::MessageSubscription& Win32Window::MessageSubscription::operator=(
		MessageSubscription&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_Window = std::exchange(other.m_Window, nullptr);
			m_Id = std::exchange(other.m_Id, 0);
		}
		return *this;
	}

	Win32Window::MessageSubscription::~MessageSubscription() noexcept
	{
		Reset();
	}

	void Win32Window::MessageSubscription::Reset() noexcept
	{
		if (m_Window)
		{
			m_Window->Unsubscribe(m_Id);
			m_Window = nullptr;
			m_Id = 0;
		}
	}

	Win32Window::Win32Window(HINSTANCE instance) noexcept :
		m_Instance(instance)
	{}

	bool Win32Window::Initialize(const PlatformWindowCreateInfo& createInfo) noexcept
	{
		if (m_Hwnd || !m_Instance || createInfo.m_Title.empty() ||
			createInfo.m_Width == 0 || createInfo.m_Height == 0)
		{
			return false;
		}

		m_ClassName.assign(createInfo.m_Title);
		m_ClassName += L".WindowClass";
		const std::wstring windowTitle(createInfo.m_Title);
		m_Width = createInfo.m_Width;
		m_Height = createInfo.m_Height;

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
			return false;
		}

		RECT windowRect{
			0,
			0,
			static_cast<LONG>(m_Width),
			static_cast<LONG>(m_Height),
		};
		AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

		m_Hwnd = CreateWindowExW(
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
			this);

		if (!m_Hwnd)
		{
			UnregisterClassW(m_ClassName.c_str(), m_Instance);
			m_ClassName.clear();
			return false;
		}

		ShowWindow(m_Hwnd, SW_SHOWDEFAULT);
		return true;
	}

	void Win32Window::Finalize() noexcept
	{
		m_MessageHandlers.clear();
		m_Events.clear();

		if (m_Hwnd)
		{
			DestroyWindow(m_Hwnd);
			m_Hwnd = nullptr;
		}

		if (!m_ClassName.empty())
		{
			UnregisterClassW(m_ClassName.c_str(), m_Instance);
			m_ClassName.clear();
		}

		m_Width = 0;
		m_Height = 0;
		m_IsMinimized = false;
		m_InSizeMove = false;
	}

	Win32Window::MessageSubscription Win32Window::Subscribe(Win32MessageHandler& handler) noexcept
	{
		const uint64_t id = m_NextMessageHandlerId++;
		m_MessageHandlers.push_back({ id, &handler });
		return MessageSubscription(this, id);
	}

	bool Win32Window::PollEvent(PlatformEvent& event) noexcept
	{
		if (m_Events.empty())
		{
			return false;
		}

		event = m_Events.front();
		m_Events.pop_front();
		return true;
	}

	LRESULT CALLBACK Win32Window::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		Win32Window* window = reinterpret_cast<Win32Window*>(
			GetWindowLongPtrW(hwnd, GWLP_USERDATA));

		if (message == WM_NCCREATE)
		{
			const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
			window = static_cast<Win32Window*>(create->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
		}

		return window ? window->HandleMessage(hwnd, message, wParam, lParam)
			: DefWindowProcW(hwnd, message, wParam, lParam);
	}

	LRESULT Win32Window::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
	{
		bool windowMessageHandled = false;
		LRESULT windowMessageResult = 0;

		switch (message)
		{
		case WM_DESTROY:
			PostQuitMessage(0);
			windowMessageHandled = true;
			break;

		case WM_PAINT:
		{
			PAINTSTRUCT paint{};
			BeginPaint(hwnd, &paint);
			EndPaint(hwnd, &paint);
			windowMessageHandled = true;
			break;
		}

		case WM_ACTIVATEAPP:
			PushEvent(wParam ? PlatformEventType::Activated : PlatformEventType::Deactivated);
			windowMessageHandled = true;
			break;

		case WM_ENTERSIZEMOVE:
			m_InSizeMove = true;
			PushEvent(PlatformEventType::Suspended);
			windowMessageHandled = true;
			break;

		case WM_EXITSIZEMOVE:
		{
			m_InSizeMove = false;
			PushEvent(PlatformEventType::Resumed);

			RECT clientRect{};
			GetClientRect(hwnd, &clientRect);
			m_Width = static_cast<uint32_t>(clientRect.right - clientRect.left);
			m_Height = static_cast<uint32_t>(clientRect.bottom - clientRect.top);
			PushEvent(PlatformEventType::Resized, m_Width, m_Height);
			windowMessageHandled = true;
			break;
		}

		case WM_SIZE:
		{
			const uint32_t width = LOWORD(lParam);
			const uint32_t height = HIWORD(lParam);

			if (wParam == SIZE_MINIMIZED)
			{
				m_IsMinimized = true;
				PushEvent(PlatformEventType::Suspended);
			}
			else
			{
				const bool wasMinimized = m_IsMinimized;
				m_IsMinimized = false;
				m_Width = width;
				m_Height = height;

				if (wasMinimized)
				{
					PushEvent(PlatformEventType::Resumed);
				}
				if (!m_InSizeMove)
				{
					PushEvent(PlatformEventType::Resized, width, height);
				}
			}
			windowMessageHandled = true;
			break;
		}

		case WM_NCDESTROY:
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
			m_Hwnd = nullptr;
			break;

		default:
			break;
		}

		const Win32MessageResult handlerResult =
			DispatchMessageHandlers(hwnd, message, wParam, lParam);
		if (handlerResult.m_Handled)
		{
			return handlerResult.m_Result;
		}

		return windowMessageHandled ? windowMessageResult
			: DefWindowProcW(hwnd, message, wParam, lParam);
	}

	Win32MessageResult Win32Window::DispatchMessageHandlers(
		HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
	{
		for (const MessageHandlerEntry& entry : m_MessageHandlers)
		{
			if (!entry.m_Handler)
			{
				continue;
			}

			const Win32MessageResult result =
				entry.m_Handler->HandleWin32Message(hwnd, message, wParam, lParam);
			if (result.m_Handled)
			{
				return result;
			}
		}

		return {};
	}

	void Win32Window::Unsubscribe(uint64_t id) noexcept
	{
		std::erase_if(m_MessageHandlers,
			[id](const MessageHandlerEntry& entry) { return entry.m_Id == id; });
	}

	void Win32Window::PushEvent(PlatformEventType type, uint32_t width, uint32_t height) noexcept
	{
		m_Events.push_back({
			.m_Type = type,
			.m_Width = width,
			.m_Height = height,
			});
	}
}
