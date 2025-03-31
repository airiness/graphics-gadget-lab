#include "Precompiled.h"
#include "Application.h"
namespace graphicsGadgetLab
{
	Application::Application(const std::wstring& windowName, uint32_t windowWidth, uint32_t windowHeight, HINSTANCE hInstance) noexcept :
		mWindowName(windowName),
		mWindowWidth(windowWidth),
		mWindowHeight(windowHeight),
		mHInstance(hInstance)
	{
	}

	void Application::Initialize() noexcept
	{
		InitializeWindow();
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
		wcex.hInstance = mHInstance;
		wcex.hIcon = LoadIcon(mHInstance, IDI_APPLICATION);
		wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wcex.lpszMenuName = nullptr;
		wcex.lpszClassName = mWindowName.data();
		wcex.hIconSm = LoadIcon(mHInstance, IDI_APPLICATION);
		RegisterClassEx(&wcex);

		RECT rc = { 0 , 0, static_cast<LONG>(mWindowWidth), static_cast<LONG>(mWindowHeight) };
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

		mHwnd = CreateWindow(
			mWindowName.data(),
			mWindowName.data(),
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			rc.right - rc.left,
			rc.bottom - rc.top,
			nullptr,
			nullptr,
			mHInstance,
			this);

		ShowWindow(mHwnd, SW_SHOWDEFAULT);
	}
}