#include "Core/Input/InputManager.h"
#include "GGLabFoundation/Platform/Win/HResult.h"
#include "Core/Input/Keyboard.h"
#include "Core/Input/Mouse.h"
#include "Core/Log/LogMacros.h"

namespace gglab
{
	bool InputManager::Initialize(void* nativeWindowHandle) noexcept
	{
		ComPtr<IGameInput> gameInput;
		const HRESULT result = GameInputCreate(&gameInput);
		if (FAILED(result))
		{
			GGLAB_LOG_ERROR("GameInput is unavailable; continuing with UI-only input: {}",
				FormatHResult(result));
		}

		m_Keyboard = std::make_unique<Keyboard>(gameInput.Get());
		m_Mouse = std::make_unique<Mouse>(gameInput.Get());
		m_Mouse->SetWindowHandle(static_cast<HWND>(nativeWindowHandle));

		const bool isAvailable = m_Keyboard->IsAvailable() && m_Mouse->IsAvailable();
		if (!isAvailable)
		{
			// Keep the Win32-backed DevelopGui usable when GameInput cannot initialize.
			m_Mouse->SetMouseMode(Mouse::MouseMode::Absolute);
		}
		return isAvailable;
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

	void InputManager::SetUICaptureState(bool keyboardCaptured, bool mouseCaptured) noexcept
	{
		m_KeyboardCapturedByUI = keyboardCaptured;
		m_MouseCapturedByUI = mouseCaptured;
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
