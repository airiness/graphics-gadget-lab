#include "Precompiled.h"
#include "Application.h"
namespace graphicsGadgetLab
{
	std::unique_ptr<Application> Application::s_Application;

	void Application::CreateApplicationInstance(const std::wstring& windowName, uint32_t windowWidth, uint32_t windowHeight, HINSTANCE hInstance) noexcept
	{
		if (s_Application == nullptr)
		{
			s_Application = std::make_unique<Application>(windowName, windowWidth, windowHeight, hInstance);
		}
	}

	Application* Application::Get() noexcept
	{	

		return s_Application.get();
	}

	Application::Application(const std::wstring& windowName, uint32_t windowWidth, uint32_t windowHeight, HINSTANCE hInstance) noexcept :
		m_WindowName(windowName),
		m_WindowWidth(windowWidth),
		m_WindowHeight(windowHeight),
		m_HInstance(hInstance)
	{
	}

	void Application::Initialize() noexcept
	{
		InitializeWindow();

		m_Renderer = std::make_unique<Renderer>();
		m_Renderer->Initialize();
	}

	void Application::Update() noexcept
	{
		MSG msg = {};
		while (msg.message != WM_QUIT)
		{
			if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	void Application::Finalize() noexcept
	{
	}

	LRESULT Application::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		Application* app = reinterpret_cast<Application*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
		switch (message)
		{
		case WM_CREATE:
		{
			LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
			SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
		}
		return 0;
		case WM_DESTROY:
		{
			PostQuitMessage(0);
		}
		return 0;
		case WM_PAINT:
		{
		}
		return 0;
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	void Application::InitializeWindow() noexcept
	{
		// Set the working directory to the path of the executable.
		WCHAR path[MAX_PATH];
		HMODULE hModule = ::GetModuleHandleW(NULL);
		if (::GetModuleFileNameW(hModule, path, MAX_PATH) > 0)
		{
			::PathRemoveFileSpecW(path);
			::SetCurrentDirectoryW(path);
		}

		WNDCLASSEX wcex = {};
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = WindowProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = 0;
		wcex.hInstance = m_HInstance;
		wcex.hIcon = LoadIcon(m_HInstance, IDI_APPLICATION);
		wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wcex.lpszMenuName = nullptr;
		wcex.lpszClassName = m_WindowName.data();
		wcex.hIconSm = LoadIcon(m_HInstance, IDI_APPLICATION);
		RegisterClassEx(&wcex);

		RECT rc = { 0 , 0, static_cast<LONG>(m_WindowWidth), static_cast<LONG>(m_WindowHeight) };
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

		m_Hwnd = CreateWindow(
			m_WindowName.data(),
			m_WindowName.data(),
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			rc.right - rc.left,
			rc.bottom - rc.top,
			nullptr,
			nullptr,
			m_HInstance,
			this);

		ShowWindow(m_Hwnd, SW_SHOWDEFAULT);
	}
}