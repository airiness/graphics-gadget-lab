#include "Core/Precompiled.h"
#include "Core/Input/Mouse.h"

namespace gglab
{
	Mouse::Mouse() noexcept :
		InputBase(GameInputKind::GameInputKindMouse)
	{}

	void Mouse::Update() noexcept
	{
		if (m_GameInput && IsConnected())
		{
			auto state = GetState();
			m_StateTracker.Update(state, m_WindowHandle);
		}

		if (GetForegroundWindow() == m_WindowHandle && m_Mode == MouseMode::Relative)
		{
			SetCursorVisible(false);
			SetClipToWindow(true);
		}
		else
		{
			SetCursorVisible(true);
			SetClipToWindow(false);
		}
	}

	void Mouse::SetWindowHandle(HWND window) noexcept
	{
		m_WindowHandle = window;
	}

	Vector2 Mouse::GetMouseCoord() const noexcept
	{
		switch (m_Mode)
		{
		case MouseMode::Absolute:
			return Vector2(
				static_cast<float>(m_StateTracker.m_AbsoluteX),
				static_cast<float>(m_StateTracker.m_AbsoluteY));
		case MouseMode::Relative:
			return Vector2(
				static_cast<float>(m_StateTracker.m_RelativeX),
				static_cast<float>(m_StateTracker.m_RelativeY));
		default:
			GGLAB_UNREACHABLE("Invalid Mouse Mode.");
		}
	}

	int64_t Mouse::GetScrollWheelDeltaY() const noexcept
	{
		return m_StateTracker.m_ScrollWheelDeltaY;
	}

	bool Mouse::IsMouseButtonPressed(MouseButton button) const noexcept
	{
		return m_StateTracker.m_ButtonPressed[button];
	}

	bool Mouse::IsMouseButtonReleased(MouseButton button) const noexcept
	{
		return m_StateTracker.m_ButtonReleased[button];
	}

	bool Mouse::IsMouseButtonHeld(MouseButton button) const noexcept
	{
		return m_StateTracker.m_ButtonHeld[button];
	}

	Mouse::MouseMode Mouse::GetMouseMode() const noexcept
	{
		return m_Mode;
	}

	void Mouse::SetMouseMode(MouseMode mode) noexcept
	{
		if (m_Mode == mode)
		{
			return;
		}

		m_Mode = mode;
	}

	bool Mouse::IsCursorVisible() const noexcept
	{
		// Get cursor info
		CURSORINFO info = {
			.cbSize = sizeof(CURSORINFO),
			.flags = 0,
			.hCursor = nullptr,
			.ptScreenPos = {} };
		if (!GetCursorInfo(&info))
		{
			return false;
		}

		return (info.flags & CURSOR_SHOWING) != 0;
	}

	void Mouse::SetCursorVisible(bool visible) const noexcept
	{
		bool isVisible = IsCursorVisible();
		if (isVisible != visible)
		{
			ShowCursor(visible);
		}
	}

	Mouse::State Mouse::GetState() noexcept
	{
		State state = {};

		ComPtr<IGameInputReading> reading;
		if (SUCCEEDED(m_GameInput->GetCurrentReading(GameInputKindMouse, nullptr, &reading)))
		{
			GameInputMouseState mouse;
			if (reading->GetMouseState(&mouse))
			{
				state.m_Buttons[MouseButton::LeftButton] = (mouse.buttons & GameInputMouseLeftButton) != 0;
				state.m_Buttons[MouseButton::MiddleButton] = (mouse.buttons & GameInputMouseMiddleButton) != 0;
				state.m_Buttons[MouseButton::RightButton] = (mouse.buttons & GameInputMouseRightButton) != 0;
				state.m_Buttons[MouseButton::X1Button] = (mouse.buttons & GameInputMouseButton4) != 0;
				state.m_Buttons[MouseButton::X2Button] = (mouse.buttons & GameInputMouseButton5) != 0;

				state.m_CoordX = mouse.positionX;
				state.m_CoordY = mouse.positionY;
				state.m_ScrollWheelY = mouse.wheelY;
			}
		}
		else
		{
			//TODO: error log
		}

		state.m_ModeState = m_Mode;

		return state;
	}

	void Mouse::SetClipToWindow(bool isClip) const noexcept
	{
		if (!isClip)
		{
			ClipCursor(nullptr);
			return;
		}

		if (!m_WindowHandle)
		{
			// TODO:error log
			return;
		}

		RECT rect;
		GetClientRect(m_WindowHandle, &rect);

		POINT ul;
		ul.x = rect.left;
		ul.y = rect.top;

		POINT lr;
		lr.x = rect.right;
		lr.y = rect.bottom;

		std::ignore = MapWindowPoints(m_WindowHandle, nullptr, &ul, 1);
		std::ignore = MapWindowPoints(m_WindowHandle, nullptr, &lr, 1);

		rect.left = ul.x;
		rect.top = ul.y;

		rect.right = lr.x;
		rect.bottom = lr.y;

		ClipCursor(&rect);

	}

	void Mouse::StateTracker::Update(const State& state, HWND windowHandle) noexcept
	{
		for (int32_t buttonIndex = 0; buttonIndex < (int32_t)MouseButtonCount; buttonIndex++)
		{
			m_ButtonPressed[buttonIndex] = state.m_Buttons[buttonIndex] && !m_LastState.m_Buttons[buttonIndex];
			m_ButtonReleased[buttonIndex] = !state.m_Buttons[buttonIndex] && m_LastState.m_Buttons[buttonIndex];
			m_ButtonHeld[buttonIndex] = state.m_Buttons[buttonIndex];
		}

		if (state.m_ModeState == MouseMode::Absolute)
		{
			POINT pt;
			if (GetCursorPos(&pt))
			{
				ScreenToClient(windowHandle, &pt);
			}

			m_AbsoluteX = static_cast<int64_t>(pt.x);
			m_AbsoluteY = static_cast<int64_t>(pt.y);
		}
		else if (state.m_ModeState == MouseMode::Relative)
		{
			m_RelativeX = state.m_CoordX - m_LastState.m_CoordX;
			m_RelativeY = state.m_CoordY - m_LastState.m_CoordY;
		}

		m_ScrollWheelDeltaY = state.m_ScrollWheelY - m_LastState.m_ScrollWheelY;

		m_LastState = state;
	}

	void Mouse::StateTracker::Reset() noexcept
	{
		*this = StateTracker();
	}
}