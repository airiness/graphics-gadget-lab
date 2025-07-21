#pragma once
namespace graphicsGadgetLab
{
	class Keyboard;
	class Mouse;
	//class GamePad;
	class InputManager final
	{
	public:
		InputManager() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(InputManager);
		~InputManager() noexcept = default;

		void Initialize(HWND windowHandle) noexcept;
		void Update() noexcept;
		void Finalize() noexcept;

		Keyboard* GetKeyboard() const noexcept;
		Mouse* GetMouse() const noexcept;

		void OnActive() noexcept;
		void OnInactive() noexcept;
		void OnSuspend() noexcept;
		void OnResume() noexcept;

	private:
		std::unique_ptr<Keyboard> m_Keyboard;

		std::unique_ptr<Mouse> m_Mouse;
	};
}