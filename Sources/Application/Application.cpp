#include "Core/Precompiled.h"
#include "Application/Application.h"
#include "Application/Platform/PlatformHost.h"
#include "Application/Platform/PlatformWindow.h"
#include "Application/Demo/DemoLabHost.h"
#include "Application/Demo/DemoManager.h"
#include "Application/Demo/DemoPlayground.h"
#include "Application/Demo/DemoTypes.h"
#include "Core/Time.h"
#include "Core/Profiling/CpuProfiler.h"
#include "Core/Input/InputManager.h"
#include "Core/Input/Keyboard.h"
#include "Core/Input/Mouse.h"
#include "Graphics/Renderer.h"
#include "Graphics/AssetManager.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/RenderFrameBuilder.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiSystem.h"
#include "DevTools/DevelopGui/Panels/DemoPanel.h"

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

	void Application::CreateApplicationInstance(CreateInfo createInfo) noexcept
	{
		if (s_Application == nullptr)
		{
			s_Application = std::make_unique<Application>(std::move(createInfo));
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

	Application::Application(CreateInfo createInfo) noexcept :
		m_WindowName(createInfo.m_WindowName),
		m_WindowWidth(createInfo.m_WindowWidth),
		m_WindowHeight(createInfo.m_WindowHeight),
		m_PlatformHost(std::move(createInfo.m_PlatformHost))
	{}

	Application::~Application() = default;

	void Application::Run() noexcept
	{
		if (!m_IsInitialized || !m_PlatformHost)
		{
			return;
		}

		while (!m_PlatformHost->IsQuitRequested())
		{
			m_PlatformHost->PumpEvents();

			PlatformEvent event{};
			while (m_PlatformHost->PollEvent(event))
			{
				HandlePlatformEvent(event);
			}

			if (m_PlatformHost->IsQuitRequested() || !Tick())
			{
				return;
			}
		}
	}

	void Application::Initialize() noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}

		// Logger
		Logger::Initialize();

		if (!m_PlatformHost)
		{
			GGLAB_LOG_ERROR("Application requires a platform host.");
			return;
		}

		const PlatformWindowCreateInfo windowCreateInfo{
			.m_Title = m_WindowName,
			.m_Width = m_WindowWidth,
			.m_Height = m_WindowHeight,
		};
		if (!m_PlatformHost->Initialize(windowCreateInfo))
		{
			GGLAB_LOG_ERROR("Failed to initialize the platform host.");
			return;
		}

		auto& mainWindow = m_PlatformHost->GetMainWindow();
		m_WindowWidth = mainWindow.GetWidth();
		m_WindowHeight = mainWindow.GetHeight();

		// Time
		m_Time = std::make_unique<Time>();
		m_Time->Initialize();

		// InputManager
		m_InputManager = std::make_unique<InputManager>();
		m_InputManager->Initialize(mainWindow.GetNativeHandle());

		// ShaderManager
		m_ShaderManager = std::make_unique<ShaderManager>();

		// Renderer
		m_Renderer = std::make_unique<Renderer>();
		Renderer::CreateInfo rendererCreateInfo{};
		rendererCreateInfo.m_ShaderManager = m_ShaderManager.get();
		rendererCreateInfo.m_NativeWindowHandle = mainWindow.GetNativeHandle();
		rendererCreateInfo.m_Width = m_WindowWidth;
		rendererCreateInfo.m_Height = m_WindowHeight;
		m_Renderer->Initialize(rendererCreateInfo);

		AssetManager::CreateInfo assetManagerCreateInfo{};
		assetManagerCreateInfo.m_Device = m_Renderer->GetDevice();
		assetManagerCreateInfo.m_TransferManager = m_Renderer->GetTransferManager();
		assetManagerCreateInfo.m_TextureRegistry = m_Renderer->GetTextureRegistry();
		assetManagerCreateInfo.m_SamplerRegistry = m_Renderer->GetSamplerRegistry();
		m_AssetManager = std::make_unique<AssetManager>(assetManagerCreateInfo);

		m_DemoManager = std::make_unique<DemoManager>();
		m_DemoManager->OnResize(m_WindowWidth, m_WindowHeight);

		InitializeAssets();

		const DemoCreateInfo demoCreateInfo{
			.m_Services = {
				.m_Renderer = m_Renderer.get(),
				.m_AssetManager = m_AssetManager.get(),
				.m_ShaderManager = m_ShaderManager.get(),
				.m_InputManager = m_InputManager.get(),
				.m_Time = m_Time.get(),
			},
			.m_WindowWidth = m_WindowWidth,
			.m_WindowHeight = m_WindowHeight,
		};
		GGLAB_UNUSED(m_DemoManager->CreateDemo<DemoPlayground>(demoCreateInfo));
		GGLAB_UNUSED(m_DemoManager->CreateDemo<DemoLabHost>(demoCreateInfo));

		m_DevelopGuiSystem = std::make_unique<DevelopGuiSystem>();
		const DevelopGuiSystem::CreateInfo developGuiCreateInfo{
			.m_Window = &mainWindow,
			.m_RHIContext = m_Renderer->GetRHIContext(),
		};
		if (!m_DevelopGuiSystem->Initialize(developGuiCreateInfo))
		{
			GGLAB_LOG_WARN("Application will continue without DevelopGui.");
		}
		else
		{
			m_DevelopGuiSystem->GetDevToolsRuntime().GetRegistry().RegisterPanel(
				std::make_unique<DemoPanel>(m_DemoManager.get()));
		}

		m_RenderFrameBuilder = std::make_unique<RenderFrameBuilder>();

		m_IsInitialized = true;
	}

	bool Application::Tick() noexcept
	{
		if (!m_IsInitialized)
		{
			return true;
		}

		if (m_IsSuspended)
		{
			m_PlatformHost->WaitForEvents();
			return true;
		}

		GGLAB_CPU_PROFILE_FRAME(m_Time->GetFrameCount() + 1);

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

		// Apply UI-driven demo changes before the demo updates or frame data is built.
		m_DemoManager->ApplyPendingActiveDemo();

		// DevelopGui new frame
		const bool developGuiFrameOpen =
			m_DevelopGuiSystem && m_DevelopGuiSystem->BeginFrame();

		// Update demo
		auto* demo = m_DemoManager->GetActiveDemo();
		demo->Update();

		auto& world = demo->GetWorld();
		auto& camera = demo->GetCamera();
		const uint32_t backBufferIndex = m_Renderer->GetSwapChain()->GetCurrentBackBufferIndex();
		// Renderer::Frame may retire RenderGraph resources from its RAII abort path.
		// Keep the graph alive until after the frame has ended.
		RenderGraph rg(m_Renderer->CreateRenderGraphCreateInfo());
		auto rendererFrame = m_Renderer->BeginFrame(backBufferIndex);

		auto& shadowVisualizationSettings =
			m_DevelopGuiSystem->GetDevToolsRuntime().GetRenderVisualizationSettings().m_Shadow;
		const RenderFrameBuilder::BuildInfo frameBuildInfo{
			.m_World = world,
			.m_Camera = camera,
			.m_Renderer = *m_Renderer,
			.m_AssetManager = *m_AssetManager,
			.m_ShadowVisualizationSettings = shadowVisualizationSettings,
			.m_WindowWidth = m_WindowWidth,
			.m_WindowHeight = m_WindowHeight,
			.m_BackBufferIndex = backBufferIndex,
		};
		RenderFrameBuilder::BuildResult frame;
		{
			GGLAB_CPU_PROFILE_SCOPE("RenderFrameBuilder");
			frame = m_RenderFrameBuilder->Build(frameBuildInfo);
		}
		RenderFrameContext renderContext = frame.MakeRenderFrameContext();

		const RenderServices services{
			.m_Renderer = m_Renderer.get(),
			.m_AssetManager = m_AssetManager.get(),
			.m_ShaderManager = m_ShaderManager.get(),
			.m_DevelopGuiSystem = m_DevelopGuiSystem.get(),
		};

		// Build RenderGraph
		auto& renderPipeline = demo->GetRenderPipeline();
		{
			GGLAB_CPU_PROFILE_SCOPE("RenderGraph Build");
			renderPipeline.BuildRenderGraph(rg, renderContext, services);
		}
		bool renderGraphCompiled = false;
		{
			GGLAB_CPU_PROFILE_SCOPE("RenderGraph Compile");
			renderGraphCompiled = rg.Compile();
		}
		GGLAB_ASSERT_MSG(renderGraphCompiled, "RenderGraph compilation failed.");
		if (!renderGraphCompiled)
		{
			if (m_DevelopGuiSystem && m_DevelopGuiSystem->IsFrameOpen())
			{
				m_DevelopGuiSystem->EndFrame();
			}
			return false;
		}

		// Draw menus before Renderer::Render()
		if (developGuiFrameOpen && m_DevelopGuiSystem->IsFrameOpen())
		{
			GGLAB_CPU_PROFILE_SCOPE("DevelopGUI");
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

			m_DevelopGuiSystem->Draw(guiContext);
		}

		// Render
		{
			GGLAB_CPU_PROFILE_SCOPE("RenderGraph Execute");
			m_Renderer->Render(rendererFrame, rg, renderContext);
		}
		{
			GGLAB_CPU_PROFILE_SCOPE("Renderer EndFrame");
			m_Renderer->EndFrame(rendererFrame);
		}

		const DemoFrameFeedback demoFeedback{
			.m_RenderSceneStatus = frame.m_RenderSceneStatus,
			.m_SubmittedFence = m_Renderer->GetLastSubmittedFencePoint(),
			.m_FrameIndex = m_Time->GetFrameCount(),
			.m_BackBufferIndex = backBufferIndex,
		};
		demo->OnFrameSubmitted(demoFeedback);

		// Pipelines without a DevelopGui render pass must still close the ImGui frame.
		if (m_DevelopGuiSystem && m_DevelopGuiSystem->IsFrameOpen())
		{
			m_DevelopGuiSystem->EndFrame();
		}

		return true;
	}

	void Application::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		// Must flush here for gpu resource safe release next
		m_Renderer->GetRHIContext()->WaitIdle();

		m_DemoManager.reset();
		m_RenderFrameBuilder.reset();
		if (m_DevelopGuiSystem)
		{
			m_DevelopGuiSystem->Finalize();
			m_DevelopGuiSystem.reset();
		}
		m_AssetManager.reset();

		m_Renderer->Finalize();
		m_Renderer.reset();

		m_ShaderManager.reset();
		m_InputManager.reset();
		m_Time.reset();

		m_PlatformHost->Finalize();

		m_IsInitialized = false;
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

	void Application::HandlePlatformEvent(const PlatformEvent& event) noexcept
	{
		switch (event.m_Type)
		{
		case PlatformEventType::Activated:
			OnActive();
			break;
		case PlatformEventType::Deactivated:
			OnInactive();
			break;
		case PlatformEventType::Suspended:
			OnSuspend();
			break;
		case PlatformEventType::Resumed:
			OnResume();
			break;
		case PlatformEventType::Resized:
			OnResize(event.m_Width, event.m_Height);
			break;
		}
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
