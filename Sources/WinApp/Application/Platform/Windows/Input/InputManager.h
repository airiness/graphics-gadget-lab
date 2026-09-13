#pragma once
#include "ApplicationInput.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <memory>

namespace gglab
{
	class Keyboard;
	class Mouse;
	// class GamePad; // todo: GamePad
	class InputManager final
	{
	public:
		InputManager() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(InputManager);
		~InputManager() noexcept;

		[[nodiscard]] bool Initialize(void* nativeWindowHandle) noexcept;
		void Update() noexcept;
		void Finalize() noexcept;

		ApplicationInput* GetApplicationInput() noexcept;
		const ApplicationInput* GetApplicationInput() const noexcept;

		void OnActive() noexcept;
		void OnInactive() noexcept;
		void OnSuspend() noexcept;
		void OnResume() noexcept;

	private:
		std::unique_ptr<Keyboard> m_Keyboard;
		std::unique_ptr<Mouse> m_Mouse;
		ApplicationInput m_ApplicationInput;
	};
}
