#pragma once
#include "Renderer.h"
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

		uint32_t GetWindowWidth() const noexcept { return m_WindowWidth; }
		uint32_t GetWindowHeight() const noexcept { return m_WindowHeight; }

		HWND GetHwnd() const noexcept { return m_Hwnd; };

		Renderer* GetRenderer() noexcept { return m_Renderer.get(); }

		static void CreateApplicationInstance(const std::wstring& windowName, uint32_t windowWidth, uint32_t windowHeight, HINSTANCE hInstance) noexcept;
		static Application* Get() noexcept;

	private:
		static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	private:
		void InitializeWindow() noexcept;

	private:
		static std::unique_ptr<Application> s_Application;

	private:
		uint32_t m_WindowWidth = 0;
		uint32_t m_WindowHeight = 0;

		std::wstring m_WindowName;

		HWND m_Hwnd = nullptr;
		HINSTANCE m_HInstance = nullptr;

		// Renderer
		std::unique_ptr<Renderer> m_Renderer;
	};
}


