#include "Precompiled.h"
#include "InputManager.h"
#include "Utility.h"
#include "Keyboard.h"

namespace graphicsGadgetLab
{
	void InputManager::Initialize() noexcept
	{
		m_Keyboard = std::make_unique<Keyboard>();
	}

	void InputManager::Update() noexcept
	{
		m_Keyboard->Update();
	}

	void InputManager::Finalize() noexcept
	{
	}

	void InputManager::OnActive() noexcept
	{
	}

	void InputManager::OnInactive() noexcept
	{
	}

	void InputManager::OnSuspend() noexcept
	{
	}

	void InputManager::OnResume() noexcept
	{
	}
}