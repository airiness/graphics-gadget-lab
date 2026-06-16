#include "Core/Precompiled.h"
#include "Core/Application.h"
#include "Graphics/Renderer.h"
#include "Graphics/AssetManager.h"
#include "Core/Input/InputManager.h"
#include "Graphics/ShaderManager.h"
#include "Core/Demo/DemoManager.h"
#include "Graphics/RenderFrameBuilder.h"
#include "Core/Time.h"
#include "Core/Input/Keyboard.h"
#include "Core/Input/Mouse.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Core/Demo/DemoPlayground.h"
#include "DevTools/DevToolsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiPanelCatalog.h"

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
	{}

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
		assetManagerCreateInfo.m_TextureRegistry = m_Renderer->GetTextureRegistry();
		assetManagerCreateInfo.m_SamplerRegistry = m_Renderer->GetSamplerRegistry();
		m_AssetManager = std::make_unique<AssetManager>(assetManagerCreateInfo);

		m_DemoManager = std::make_unique<DemoManager>();

		InitializeAssets();
		m_DevToolsRuntime = std::make_unique<DevToolsRuntime>();
		InitializeDevToolsPanels();

		// InitializeDemos
		const auto demoIndex = m_DemoManager->CreateDemo<DemoPlayground>();
		m_DemoManager->SetActiveDemo(demoIndex);

		m_RenderFrameBuilder = std::make_unique<RenderFrameBuilder>();

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

		// DevelopGui new frame
		auto* developGuiBackend = m_Renderer->GetDevelopGuiBackend();
		if (developGuiBackend)
		{
			developGuiBackend->NewFrame();
		}

		// Update demo
		auto* demo = m_DemoManager->GetActiveDemo();
		demo->Update();

		auto& world = demo->GetWorld();
		auto& camera = demo->GetCamera();
		const RenderFrameBuilder::BuildInfo frameBuildInfo{
			.m_World = world,
			.m_Camera = camera,
			.m_Renderer = *m_Renderer,
			.m_AssetManager = *m_AssetManager,
			.m_ShadowVisualizationSettings = m_DevToolsRuntime->GetRenderVisualizationSettings().m_Shadow,
			.m_WindowWidth = m_WindowWidth,
			.m_WindowHeight = m_WindowHeight,
			.m_BackBufferIndex = m_Renderer->GetSwapChain()->GetCurrentBackBufferIndex(),
		};
		auto frame = m_RenderFrameBuilder->Build(frameBuildInfo);
		RenderFrameContext renderContext = frame.MakeRenderFrameContext();

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

		// Draw menus before Renderer::Render()
		if (m_DevToolsRuntime)
		{
			DevelopGuiContext guiContext{};
			guiContext.m_Camera = &camera;
			guiContext.m_CameraController = &demo->GetCameraController();
			guiContext.m_Renderer = m_Renderer.get();
			guiContext.m_World = &world;
			guiContext.m_RenderViews = std::span<RenderView>(frame.m_RenderViews);
			guiContext.m_MainRenderView = &frame.m_RenderViews[utils::ToIndex(RenderViewID::Main)];
			guiContext.m_AssetManager = m_AssetManager.get();
			guiContext.m_RenderGraph = &rg;
			guiContext.m_DirectionalShadowSettings = frame.m_WorldData.m_MainDirectionalLight.m_ShadowSettings;

			m_DevToolsRuntime->Draw(guiContext);
		}

		// Render
		m_Renderer->Render(rg, renderContext);

		return true;
	}

	void Application::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		// Must flush here for gpu resource safe release next
		m_Renderer->GetDevice()->FlushGPU();

		m_DemoManager.reset();
		m_RenderFrameBuilder.reset();
		if (m_DevToolsRuntime)
		{
			m_DevToolsRuntime->Reset();
			m_DevToolsRuntime.reset();
		}
		m_AssetManager.reset();

		m_Renderer->Finalize();
		m_Renderer.reset();

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

			// Forward PBR
			desc.m_SourcePath = L"Assets/Shaders/Passes/PassForwardPBR.hlsl";
			desc.m_Stage = ShaderStage::Vertex;
			shaderDescs.push_back(desc);
			desc.m_Stage = ShaderStage::Pixel;
			shaderDescs.push_back(desc);

			// Directional Shadow Map
			desc.m_SourcePath = L"Assets/Shaders/Passes/PassDirectionalShadowMap.hlsl";
			desc.m_Stage = ShaderStage::Vertex;
			shaderDescs.push_back(desc);
			desc.m_Stage = ShaderStage::Pixel;
			shaderDescs.push_back(desc);

			// Shadow Map Preview
			desc.m_SourcePath = L"Assets/Shaders/Passes/PassShadowMapPreview.hlsl";
			desc.m_Stage = ShaderStage::Vertex;
			shaderDescs.push_back(desc);
			desc.m_Stage = ShaderStage::Pixel;
			shaderDescs.push_back(desc);

			// Tonemap
			desc.m_SourcePath = L"Assets/Shaders/Passes/PassTonemap.hlsl";
			desc.m_Stage = ShaderStage::Vertex;
			shaderDescs.push_back(desc);
			desc.m_Stage = ShaderStage::Pixel;
			shaderDescs.push_back(desc);

			m_ShaderManager->Preload(shaderDescs);
		}
	}

	void Application::InitializeDevToolsPanels() noexcept
	{
		devtools::RegisterDefaultDevelopGuiPanels(m_DevToolsRuntime->GetRegistry());
	}

	void Application::OnActive() noexcept
	{}

	void Application::OnInactive() noexcept
	{}

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
