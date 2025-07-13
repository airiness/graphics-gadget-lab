#pragma once

namespace graphicsGadgetLab
{
	class Keyboard;
	//class Mouse;
	//class GamePad;
	class InputManager final
	{
	public:
		InputManager() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(InputManager);
		~InputManager() noexcept = default;

		void Initialize() noexcept;
		void Update() noexcept;
		void Finalize() noexcept;

		Keyboard* GetKeyboard() const noexcept { return m_Keyboard.get(); }

		void OnActive() noexcept;
		void OnInactive() noexcept;
		void OnSuspend() noexcept;
		void OnResume() noexcept;

	private:
		std::unique_ptr<Keyboard> m_Keyboard;
	};
}