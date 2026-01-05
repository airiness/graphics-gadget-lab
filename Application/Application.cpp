#include "Precompiled.h"
#include "Application.h"
#include "Renderer.h"
#include "AssetManager.h"
#include "InputManager.h"
#include "ShaderManager.h"
#include "DemoManager.h"
#include "RenderViewBuilder.h"
#include "RenderSceneBuilder.h"
#include "Time.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "RenderPipelineBase.h"
#include "DemoPlayground.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

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

	Mouse* Application::GetMouse() const noexcept
	{
		if (const auto input = GetInputManager())
		{
			return input->GetMouse();
		}

		return nullptr;
	}

	void Application::CreateApplicationInstance(const CreateInfo& createInfo) noexcept
	{
		if (s_Application == nullptr)
		{
			s_Application = std::make_unique<Application>(createInfo);
			s_Application->Initialize();
		}
	}

	Application* Application::GetInstance() noexcept
	{
		GGLAB_ASSERT_MSG(s_Application != nullptr,
			"Application instance is not created. Call CreateApplicationInstance first.");
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

	Application::Application(const CreateInfo& createInfo) noexcept :
		m_WindowName(createInfo.m_WindowName),
		m_WindowWidth(createInfo.m_WindowWidth),
		m_WindowHeight(createInfo.m_WindowHeight),
		m_HInstance(createInfo.m_HInstance)
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
				if (!Tick())
				{
					return;
				}
			}
		}
	}

	LRESULT Application::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		Application* app = reinterpret_cast<Application*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

		if (app && app->m_IsInitialized && ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
		{
			return 1;
		}

		switch (message)
		{
		case WM_CREATE:
		{
			LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
			SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
			return 0;
		}
		case WM_DESTROY:
		{
			PostQuitMessage(0);
			return 0;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps{};
			BeginPaint(hWnd, &ps);
			EndPaint(hWnd, &ps);
			return 0;
		}
		case WM_ACTIVATEAPP:
		{
			if (!app)
			{
				break;
			}
			if (wParam)
			{
				app->OnActive();
			}
			else
			{
				app->OnInactive();
			}
			return 0;
		}
		case WM_ENTERSIZEMOVE:
		{
			if (!app)
			{
				break;
			}

			app->m_InSizeMove = true;
			app->OnSuspend();
			return 0;
		}
		case WM_EXITSIZEMOVE:
		{
			if (!app)
			{
				break;
			}

			app->m_InSizeMove = false;
			app->OnResume();

			RECT rc{};
			GetClientRect(hWnd, &rc);
			const auto width = static_cast<uint32_t>(rc.right - rc.left);
			const auto height = static_cast<uint32_t>(rc.bottom - rc.top);
			app->OnResize(width, height);

			return 0;
		}
		case WM_SIZE:
		{
			if (!app)
			{
				break;
			}

			const uint32_t width = LOWORD(lParam);
			const uint32_t height = HIWORD(lParam);

			if (wParam == SIZE_MINIMIZED)
			{
				app->m_IsMinimized = true;
				app->OnSuspend();
				return 0;
			}

			const bool wasMinimized = app->m_IsMinimized;
			app->m_IsMinimized = false;

			if (wasMinimized)
			{
				app->OnResume();
			}

			if (!app->m_InSizeMove)
			{
				app->OnResize(width, height);
			}
			return 0;
		}
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

		// ShaderManager
		m_ShaderManager = std::make_unique<ShaderManager>();

		// Renderer
		m_Renderer = std::make_unique<Renderer>();
		Renderer::CreateInfo rendererCreateInfo{};
		rendererCreateInfo.m_ShaderManager = m_ShaderManager.get();
		rendererCreateInfo.m_Hwnd = m_Hwnd;
		rendererCreateInfo.m_Width = m_WindowWidth;
		rendererCreateInfo.m_Height = m_WindowHeight;
		m_Renderer->Initialize(rendererCreateInfo);

		AssetManager::CreateInfo assetManagerCreateInfo{};
		assetManagerCreateInfo.m_DX12Device = m_Renderer->GetDevice();
		assetManagerCreateInfo.m_TransferManager = m_Renderer->GetTransferManager();
		assetManagerCreateInfo.m_DescriptorManager = m_Renderer->GetDescriptorManager();
		m_AssetManager = std::make_unique<AssetManager>(assetManagerCreateInfo);

		m_DemoManager = std::make_unique<DemoManager>();

		InitializeAssets();

		// InitializeDemos
		const auto demoIndex = m_DemoManager->CreateDemo<DemoPlayground>();
		m_DemoManager->SetActiveDemo(demoIndex);

		m_RenderViewBuilder = std::make_unique<RenderViewBuilder>();
		m_RenderSceneBuilder = std::make_unique<RenderSceneBuilder>();

		m_IsInitialized = true;
	}

	bool Application::Tick() noexcept
	{
		if (!m_IsInitialized)
		{
			return true;
		}

		if (m_IsMinimized || m_IsSuspended)
		{
			WaitMessage();
			return true;
		}

		m_Time->Update();
		m_InputManager->Update();

		// Toggle Mouse Input Mode
		if (const auto keyboard = GetKeyboard())
		{
			if (keyboard->IsKeyPressed(KeyCode::T))
			{

				if (const auto mouse = GetMouse())
				{
					mouse->SetMouseMode(
						(mouse->GetMouseMode() == Mouse::MouseMode::Absolute) ?
						Mouse::MouseMode::Relative :
						Mouse::MouseMode::Absolute);
				}
			}

			// Exit application when ESC pressed
			if (keyboard->IsKeyPressed(KeyCode::Escape))
			{
				return false;
			}
		}

		auto* developGui = m_Renderer->GetDevelopGui();
		if (developGui)
		{
			developGui->NewFrame();

			// ImGui test
			ImGui::ShowDemoWindow();
		}

		// Update demo
		auto* demo = m_DemoManager->GetActiveDemo();
		demo->Update();

		// Build render view
		const auto& camera = demo->GetCamera();
		const RenderViewBuilder::BuildInfo viewBuildInfo{
			.m_Camera = camera,
			.m_Width = m_WindowWidth,
			.m_Height = m_WindowHeight
		};
		RenderView renderView = m_RenderViewBuilder->Build(viewBuildInfo);

		// Build render scene
		const auto& world = demo->GetWorld();
		const RenderSceneBuilder::BuildInfo sceneBuildInfo{
			.m_World = world,
			.m_RenderView = renderView,
			.m_AssetManager = *m_AssetManager,
			.m_TransferManager = *m_Renderer->GetTransferManager(),
			.m_ObjectsSB = *m_Renderer->GetObjectStructuredBuffer(),
			.m_MaterialsSB = *m_Renderer->GetMaterialStructuredBuffer()
		};
		const auto [renderScene, uploadFencePoint] = m_RenderSceneBuilder->Build(sceneBuildInfo);

		// Update frame constant buffer
		m_Renderer->UpdateFrameConstants(renderView, renderScene);

		const RenderFrameContext renderContext{
			.m_RenderView = &renderView,
			.m_RenderScene = &renderScene,
			.m_BackBufferIndex = m_Renderer->GetSwapChain()->GetCurrentBackBufferIndex(),
			.m_UploadFencePoint = uploadFencePoint
		};

		const RenderServices services{
			.m_Renderer = m_Renderer.get(),
			.m_AssetManager = m_AssetManager.get(),
			.m_ShaderManager = m_ShaderManager.get()
		};

		// Build RenderGraph
		RenderGraph rg(m_Renderer->CreateRenderGraphCreateInfo());
		auto& renderPipeline = demo->GetRenderPipeline();
		renderPipeline.BuildRenderGraph(rg, renderContext, services);
		rg.Compile();

		// Render
		m_Renderer->Render(rg, renderContext);

		if (developGui)
		{
			developGui->EndFrame();
		}

		return true;
	}

	void Application::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		if (m_Renderer)
		{
			// Must flush here for gpu resource safe release next
			m_Renderer->GetDevice()->FlushGPU();
		}

		m_DemoManager.reset();
		m_AssetManager.reset();

		if (m_Renderer)
		{
			m_Renderer->Finalize();
			m_Renderer.reset();
		}

		m_ShaderManager.reset();
		m_InputManager.reset();
		m_Time.reset();

		m_IsInitialized = false;
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
		wcex.lpszClassName = m_WindowName.c_str();
		wcex.hIconSm = LoadIcon(m_HInstance, IDI_APPLICATION);
		RegisterClassEx(&wcex);

		RECT rc = { 0 , 0, static_cast<LONG>(m_WindowWidth), static_cast<LONG>(m_WindowHeight) };
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

		m_Hwnd = CreateWindow(
			m_WindowName.c_str(),
			m_WindowName.c_str(),
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
			std::vector<ShaderDesc> shaderDescs;
			ShaderDesc desc{};

			// Textured Model Pass
			desc.m_SourcePath = L"Assets/Shaders/Passes/PassTexturedModel.hlsl";
			desc.m_Stage = ShaderStage::Vertex;
			shaderDescs.push_back(desc);
			desc.m_Stage = ShaderStage::Pixel;
			shaderDescs.push_back(desc);

			// Forward PBR
			desc.m_SourcePath = L"Assets/Shaders/Passes/PassForwardPBR.hlsl";
			desc.m_Stage = ShaderStage::Vertex;
			shaderDescs.push_back(desc);
			desc.m_Stage = ShaderStage::Pixel;
			shaderDescs.push_back(desc);

			m_ShaderManager->Preload(shaderDescs);
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
		if (m_IsSuspended)
		{
			return;
		}

		m_IsSuspended = true;

		if (m_Renderer)
		{
			m_Renderer->OnSuspend();
		}
	}

	void Application::OnResume() noexcept
	{
		if (!m_IsSuspended)
		{
			return;
		}

		m_IsSuspended = false;

		if (m_Renderer)
		{
			m_Renderer->OnResume();
		}
	}

	void Application::OnResize(uint32_t width, uint32_t height) noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		if (width == 0 || height == 0)
		{
			return;
		}

		if (width == m_WindowWidth && height == m_WindowHeight)
		{
			return;
		}

		m_WindowWidth = width;
		m_WindowHeight = height;

		if (m_Renderer)
		{
			m_Renderer->OnResize(width, height);
		}

		if (m_DemoManager)
		{
			m_DemoManager->OnResize(width, height);
		}
	}
}