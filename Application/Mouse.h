#pragma once
#include "InputBase.h"

namespace graphicsGadgetLab
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
			Absolute,
			Relative
		};
	private:
		struct State
		{
			int32_t m_CoordX = 0;
			int32_t m_CoordY = 0;
			int32_t m_ScrollWheelValue = 0;

			bool m_Buttons[MouseButtonCount] = {};
		};

		struct StateTracker
		{
			State m_LastState;
			bool m_ButtonPressed[MouseButtonCount];
			bool m_ButtonReleased[MouseButtonCount];

			int32_t m_DeltaPositionY = 0;



			StateTracker() noexcept { Reset(); }
			void Update(const State& state) noexcept;
			void Reset() noexcept;
		};

	public:
		Mouse() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Mouse);
		virtual ~Mouse() noexcept = default;

		virtual void Update() noexcept override;

		void SetWindowHandle(HWND window) noexcept;
		Vector2 GetPosition() const noexcept;
		Vector2 GetDeltaPosition() const noexcept;

		bool IsMouseButtonPressed(MouseButton button) const noexcept;
		bool IsMouseButtonReleased(MouseButton button) const noexcept;
		bool IsMouseButtonHeld(MouseButton button) const noexcept;

		MouseMode GetMouseMode() const noexcept;
		void SetMouseMode(MouseMode mode) noexcept;

		bool IsCursorVisible() const noexcept;
		void SetCursorVisible(bool visible) const noexcept;

	private:
		State GetState() noexcept;

		void ClipToWindow() const noexcept;

	private:
		StateTracker m_StateTracker;
		MouseMode m_Mode = MouseMode::Relative;

		int64_t m_RelativeX = std::numeric_limits<int64_t>::max();
		int64_t m_RelativeY = std::numeric_limits<int64_t>::max();
		int64_t m_LastX = std::numeric_limits<int64_t>::max();
		int64_t m_LastY = std::numeric_limits<int64_t>::max();



		HWND m_WindowHandle = nullptr;

	};
}