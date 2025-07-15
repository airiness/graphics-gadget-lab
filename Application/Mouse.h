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

			MouseMode m_Mode = MouseMode::Absolute;

			StateTracker() noexcept { Reset(); }
			void Update(const State& state) noexcept;
			void Reset() noexcept;
		};

	public:
		Mouse() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Mouse);
		virtual ~Mouse() noexcept = default;

		virtual void Update() noexcept override;

		Vector2 GetPosition() const noexcept;
		Vector2 GetDeltaPosition() const noexcept;

		bool IsMouseButtonPressed(MouseButton button) const noexcept;
		bool IsMouseButtonReleased(MouseButton button) const noexcept;
		bool IsMouseButtonHeld(MouseButton button) const noexcept;

		MouseMode GetMouseMode() const noexcept;

	private:
		State GetState() noexcept;

	private:
		StateTracker m_StateTracker;

	};
}