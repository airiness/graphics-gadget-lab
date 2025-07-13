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
		GetState();

	}
	void Mouse::GetState() noexcept
	{
		if (m_GameInput)
		{
			ComPtr<IGameInputReading> reading;
			if (SUCCEEDED(m_GameInput->GetCurrentReading(GameInputKindMouse, nullptr, &reading)))
			{
				GameInputMouseState mouse;
				if (reading->GetMouseState(&mouse))
				{
					std::cout<<"mouse-left:"<<(mouse.buttons& GameInputMouseLeftButton )<<std::endl;
					std::cout<<"mouse-right:"<<(mouse.buttons& GameInputMouseRightButton)<<std::endl;
					std::cout<<"mouse-middle:"<<(mouse.buttons& GameInputMouseMiddleButton)<<std::endl;
					std::cout<<"mouse-buttons:"<<(mouse.buttons)<<std::endl;
					std::cout<<"mouse-posx:"<<mouse.positionX	 <<std::endl;
					std::cout<<"mouse-posy:"<<mouse.positionY	 <<std::endl;
					std::cout<<"mouse-wheelx:"<<mouse.wheelX	 <<std::endl;
					std::cout<<"mouse-wheely:"<<mouse.wheelY	 <<std::endl;

					int a = 6; //todo break point test.
				}
			}
		}

	}
}