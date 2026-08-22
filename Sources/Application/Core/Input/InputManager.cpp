#include "Core/Input/InputManager.h"
#include "AppRuntimeLog.h"
#include "GGLabFoundation/Platform/Win/HResult.h"
#include "Core/Input/Keyboard.h"
#include "Core/Input/Mouse.h"
#include "Core/Input/WindowsInputMapping.h"

#include <array>
#include <utility>

namespace gglab
{
	InputManager::InputManager() noexcept = default;
	InputManager::~InputManager() noexcept = default;

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
			m_ApplicationInput.SetPointerMode(AppPointerMode::Absolute);
		}
		return isAvailable;
	}

	void InputManager::Update() noexcept
	{
		const AppPointerMode pointerMode = m_ApplicationInput.GetPointerMode();
		m_Mouse->SetMouseMode(pointerMode == AppPointerMode::Absolute
			? Mouse::MouseMode::Absolute
			: Mouse::MouseMode::Relative);
		m_Keyboard->Update();
		m_Mouse->Update();

		ApplicationInputSnapshot snapshot{};
		for (const WindowsKeyMapping& mapping : GetWindowsKeyMappings())
		{
			const size_t index = static_cast<size_t>(mapping.m_Key);
			snapshot.m_KeysHeld[index] =
				m_Keyboard->IsKeyHeld(static_cast<KeyCode>(mapping.m_VirtualKey));
		}

		const std::array buttonMappings{
			std::pair{ AppPointerButton::Left, MouseButton::LeftButton },
			std::pair{ AppPointerButton::Middle, MouseButton::MiddleButton },
			std::pair{ AppPointerButton::Right, MouseButton::RightButton },
			std::pair{ AppPointerButton::X1, MouseButton::X1Button },
			std::pair{ AppPointerButton::X2, MouseButton::X2Button },
		};
		for (const auto& [button, windowsButton] : buttonMappings)
		{
			snapshot.m_PointerButtonsHeld[static_cast<size_t>(button)] =
				m_Mouse->IsMouseButtonHeld(windowsButton);
		}

		const Vector2 pointerPosition = m_Mouse->GetAbsolutePosition();
		const Vector2 pointerDelta = m_Mouse->GetRelativeDelta();
		snapshot.m_PointerPosition = { pointerPosition.m_X, pointerPosition.m_Y };
		snapshot.m_PointerDelta = { pointerDelta.m_X, pointerDelta.m_Y };
		snapshot.m_ScrollDeltaY = m_Mouse->GetScrollWheelDeltaY();
		snapshot.m_IsAvailable = m_Keyboard->IsAvailable() && m_Mouse->IsAvailable();
		m_ApplicationInput.Publish(snapshot);
	}

	void InputManager::Finalize() noexcept
	{
		if (m_Mouse)
		{
			m_Mouse->Reset();
		}
		m_ApplicationInput.Reset();
	}

	ApplicationInput* InputManager::GetApplicationInput() noexcept
	{
		return &m_ApplicationInput;
	}

	const ApplicationInput* InputManager::GetApplicationInput() const noexcept
	{
		return &m_ApplicationInput;
	}

	void InputManager::OnActive() noexcept
	{
		m_Keyboard->Reset();
		m_Mouse->Reset();
		m_ApplicationInput.Reset();
	}

	void InputManager::OnInactive() noexcept
	{
		m_Keyboard->Reset();
		m_Mouse->Reset();
		m_ApplicationInput.Reset();
	}

	void InputManager::OnSuspend() noexcept
	{
		m_Keyboard->Reset();
		m_Mouse->Reset();
		m_ApplicationInput.Reset();
	}

	void InputManager::OnResume() noexcept
	{
		m_Keyboard->Reset();
		m_Mouse->Reset();
		m_ApplicationInput.Reset();
	}
}
