#pragma once
#include "Core/Input/InputBase.h"
#include "Core/Math/MathTypes.h"

namespace gglab
{
	enum MouseButton
	{
		LeftButton,
		MiddleButton,
		RightButton,
		X1Button,
		X2Button,

		MouseButtonCount
	};

	class Mouse final : public InputBase
	{
	public:
		enum class MouseMode : uint32_t
		{
			Absolute,	// absolute coordinate: used in ui, debug menu and so on.
			Relative	// relative coordinate: used in fps camera and so on.
		};
	private:
		struct State
		{
			int64_t m_CoordX = 0;
			int64_t m_CoordY = 0;
			int64_t m_ScrollWheelY = 0;

			bool m_Buttons[MouseButtonCount] = {};

			MouseMode m_ModeState = MouseMode::Absolute;
		};

		struct StateTracker
		{
			State m_LastState;
			bool m_ButtonPressed[MouseButtonCount] = {};
			bool m_ButtonReleased[MouseButtonCount] = {};
			bool m_ButtonHeld[MouseButtonCount] = {};

			int64_t m_ScrollWheelDeltaY = 0;
			int64_t m_RelativeX = 0;
			int64_t m_RelativeY = 0;
			int64_t m_AbsoluteX = 0;
			int64_t m_AbsoluteY = 0;

			StateTracker() noexcept = default;
			void Update(const State& state, HWND windowHandle) noexcept;
			void Reset() noexcept;
		};

	public:
		Mouse() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Mouse);
		virtual ~Mouse() noexcept = default;

		virtual void Update() noexcept override;

		void SetWindowHandle(HWND window) noexcept;

		Vector2 GetMouseCoord() const noexcept;
		int64_t GetScrollWheelDeltaY() const noexcept;

		bool IsMouseButtonPressed(MouseButton button) const noexcept;
		bool IsMouseButtonReleased(MouseButton button) const noexcept;
		bool IsMouseButtonHeld(MouseButton button) const noexcept;

		MouseMode GetMouseMode() const noexcept;
		void SetMouseMode(MouseMode mode) noexcept;

		bool IsCursorVisible() const noexcept;
		void SetCursorVisible(bool visible) const noexcept;

		void SetClipToWindow(bool isClip) const noexcept;

	private:
		State GetState() noexcept;

	private:
		StateTracker m_StateTracker;
		MouseMode m_Mode = MouseMode::Relative;
		HWND m_WindowHandle = nullptr;
	};
}
