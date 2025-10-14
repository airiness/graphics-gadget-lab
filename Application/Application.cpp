#include "Precompiled.h"
#include "Application.h"
#include "Renderer.h"
#include "AssetManager.h"
#include "InputManager.h"
#include "ShaderManager.h"
#include "Time.h"
#include "Keyboard.h"
#include "Mouse.h"

namespace gglab
{
	std::unique_ptr<Application> Application::s_Application;

	Keyboard* Application::GetKeyboard() const noexcept
	{
		if (const auto input = GetInputManager())
		{
			return input->GetKeyboard();
		}

		return nullptr;
	}

	Mouse* Application::GetMouse() const noexcept\
	{
		if (const auto input = GetInputManager())
		{
			return input->GetMouse();
		}

		return nullptr;
	}

	void Application::CreateApplicationInstance(const std::wstring& windowName, uint32_t windowWidth, uint32_t windowHeight, HINSTANCE hInstance) noexcept
	{
		if (s_Application == nullptr)
		{
			s_Application = std::make_unique<Application>(windowName, windowWidth, windowHeight, hInstance);
			s_Application->Initialize();
		}
	}

	Application* Application::GetInstance() noexcept
	{
		GGLAB_ASSERT_MSG(s_Application != nullptr, "Application instance is not created. Call CreateApplicationInstance first.");
		return s_Application.get();
	}

	void Application::DestroyApplicationInstance() noexcept
	{
		if (s_Application)
		{
			s_Application->Finalize();
			s_Application.reset();
		}
	}

	Application::Application(const std::wstring& windowName, uint32_t windowWidth, uint32_t windowHeight, HINSTANCE hInstance) noexcept :
		m_WindowName(windowName),
		m_WindowWidth(windowWidth),
		m_WindowHeight(windowHeight),
		m_HInstance(hInstance)
	{
	}

	void Application::Run() noexcept
	{
		MSG msg = {};
		while (msg.message != WM_QUIT)
		{
			if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			else
			{
				Update();
			}
		}
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
		break;
		case WM_DESTROY:
		{
			PostQuitMessage(0);
		}
		break;
		case WM_PAINT:
		{
		}
		break;
		case WM_SIZE:
		{
			if (wParam == SIZE_MINIMIZED)
			{
				if (app)
				{
					int32_t width = LOWORD(lParam);
					int32_t height = HIWORD(lParam);
					app->OnResize(width, height);
				}
			}
		}
		break;
		default:
		{
		}
		break;
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	void Application::Initialize() noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}

		// Logger
		Logger::Initialize();

		// Windows
		InitializeWindow();

		// Time
		m_Time = std::make_unique<Time>();
		m_Time->Initialize();

		// InputManager
		m_InputManager = std::make_unique<InputManager>();
		m_InputManager->Initialize(m_Hwnd);

		m_Renderer = std::make_unique<Renderer>();
		m_AssetManager = std::make_unique<AssetManager>(m_Renderer->GetDevice());
		m_ShaderManager = std::make_unique<ShaderManager>();

		m_Renderer->Initialize();

		InitializeAssets();

		m_IsInitialized = true;
	}

	void Application::Update() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		m_Time->Update();
		m_InputManager->Update();

		// Toggle Mouse Input Mode
		auto keyboard = GetKeyboard();
		if (keyboard && keyboard->IsKeyPressed(KeyCode::T))
		{
			if (auto mouse = GetMouse())
			{
				mouse->SetMouseMode(
					(mouse->GetMouseMode() == Mouse::MouseMode::Absolute) ?
					Mouse::MouseMode::Relative :
					Mouse::MouseMode::Absolute);
			}
		}

		m_Renderer->Update();
		m_Renderer->Render();
	}

	void Application::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		m_Renderer->Finalize();
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

	void Application::InitializeAssets() noexcept
	{
		// Shader preload
		{
			//m_ShaderManager->Preload({}); // TODO: Preload shaders
		}

		// Test Sponza
		{
			auto model = m_AssetManager->LoadModel("Assets/Models/Sponza/Sponza.gltf");
			auto sponzaEntity = m_Registry.create();
			m_Registry.emplace<Transform>(sponzaEntity, Transform());
			m_Registry.emplace<Model>(sponzaEntity, std::move(model));
		}
	}

	void Application::OnActive() noexcept
	{
	}

	void Application::OnInactive() noexcept
	{
	}

	void Application::OnSuspend() noexcept
	{
	}

	void Application::OnResume() noexcept
	{
	}

	void Application::OnResize(int32_t width, int32_t height) noexcept
	{
	}
}