#include "Precompiled.h"
#include "Mouse.h"
#include "Utility.h"
#include <iostream>
namespace graphicsGadgetLab
{
	Mouse::Mouse() noexcept :
		InputBase(GameInputKind::GameInputKindMouse)
	{
	}

	void Mouse::Update() noexcept
	{
		if (m_GameInput && IsConnected())
		{
			auto state = GetState();
			m_StateTracker.Update(state);
		}

	}

	bool Mouse::IsMouseButtonPressed(MouseButton button) const noexcept
	{
		return false;
	}

	bool Mouse::IsMouseButtonReleased(MouseButton button) const noexcept
	{
		return false;
	}

	bool Mouse::IsMouseButtonHeld(MouseButton button) const noexcept
	{
		return false;
	}

	Mouse::MouseMode Mouse::GetMouseMode() const noexcept
	{
		return MouseMode();
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
				state.m_ScrollWheelValue = mouse.wheelY;


				std::cout << "mouse-left:" << (mouse.buttons & GameInputMouseLeftButton) << std::endl;
				std::cout << "mouse-right:" << (mouse.buttons & GameInputMouseRightButton) << std::endl;
				std::cout << "mouse-middle:" << (mouse.buttons & GameInputMouseMiddleButton) << std::endl;
				std::cout << "mouse-buttons:" << (mouse.buttons) << std::endl;
				std::cout << "mouse-posx:" << mouse.positionX << std::endl;
				std::cout << "mouse-posy:" << mouse.positionY << std::endl;
				std::cout << "mouse-wheelx:" << mouse.wheelX << std::endl;
				std::cout << "mouse-wheely:" << mouse.wheelY << std::endl;

				
			}
		}

		return state;
	}



	void Mouse::StateTracker::Update(const State& state) noexcept
	{

	}

	void Mouse::StateTracker::Reset() noexcept
	{

	}
}