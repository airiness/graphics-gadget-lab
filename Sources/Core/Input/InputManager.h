#pragma once
#include "Core/CoreMacros.h"

#include <memory>

namespace gglab
{
	class Keyboard;
	class Mouse;
	// class GamePad; // todo: GamePad
	class InputManager final
	{
	public:
		InputManager() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(InputManager);
		~InputManager() noexcept = default;

		[[nodiscard]] bool Initialize(void* nativeWindowHandle) noexcept;
		void Update() noexcept;
		void Finalize() noexcept;

		Keyboard* GetKeyboard() const noexcept;
		Mouse* GetMouse() const noexcept;
		void SetUICaptureState(bool keyboardCaptured, bool mouseCaptured) noexcept;
		[[nodiscard]] bool IsKeyboardCapturedByUI() const noexcept
		{
			return m_KeyboardCapturedByUI;
		}
		[[nodiscard]] bool IsMouseCapturedByUI() const noexcept
		{
			return m_MouseCapturedByUI;
		}

		void OnActive() noexcept;
		void OnInactive() noexcept;
		void OnSuspend() noexcept;
		void OnResume() noexcept;

	private:
		std::unique_ptr<Keyboard> m_Keyboard;
		std::unique_ptr<Mouse> m_Mouse;
		bool m_KeyboardCapturedByUI = false;
		bool m_MouseCapturedByUI = false;
	};
}
