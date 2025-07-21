#include "Precompiled.h"
#include "InputManager.h"
#include "Utility.h"
#include "Keyboard.h"
#include "Mouse.h"

namespace graphicsGadgetLab
{
	void InputManager::Initialize(HWND windowHandle) noexcept
	{
		m_Keyboard = std::make_unique<Keyboard>();
		m_Mouse = std::make_unique<Mouse>();
		m_Mouse->SetWindowHandle(windowHandle);
	}

	void InputManager::Update() noexcept
	{
		m_Keyboard->Update();
		m_Mouse->Update();
	}

	void InputManager::Finalize() noexcept
	{
	}

	Keyboard* InputManager::GetKeyboard() const noexcept
	{
		return m_Keyboard.get(); 
	}

	Mouse* InputManager::GetMouse() const noexcept
	{
		return m_Mouse.get();
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