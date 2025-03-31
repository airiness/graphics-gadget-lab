#pragma once
namespace graphicsGadgetLab
{
	class Application
	{
	public:
		explicit Application(const std::wstring& windowName, uint32_t windowWidth, uint32_t windowHeight, HINSTANCE hInstance) noexcept;
		~Application() noexcept = default;
		void Initialize() noexcept;
		void Update() noexcept;
		void Finalize() noexcept;
	private:
		static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	private:
		void InitializeWindow() noexcept;

	private:
		uint32_t mWindowWidth = 0;
		uint32_t mWindowHeight = 0;

		std::wstring mWindowName;

		HWND mHwnd = nullptr;
		HINSTANCE mHInstance = nullptr;

	};
}


