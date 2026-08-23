#include "Application/Application.h"
#include "AppRuntimeLog.h"
#include "GGLabAppRuntime.h"
#include "Application/Platform/PlatformHost.h"
#include "Application/Platform/PlatformWindow.h"
#include "Application/Platform/Windows/Win32RHIContextFactory.h"
#include "Application/Tooling/ApplicationToolingComposition.h"
#include "Application/Demo/DemoLabRuntimeLocator.h"
#include "Demo/DemoManager.h"
#include "Demo/DemoTypes.h"
#include "ApplicationToolingIntegration.h"
#include "ApplicationInput.h"
#include "LoadingProgress.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "Core/Time.h"
#include "Core/Profiling/CpuProfiler.h"
#include "Core/Input/InputManager.h"
#include "Graphics/Renderer.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/EnvironmentAssetController.h"
#include "Graphics/CameraRig.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/RenderFrameBuilder.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/DebugDraw/DebugDrawSystem.h"

#include <optional>
#include <span>
#include <utility>

namespace gglab
{
	Application::Application(CreateInfo createInfo) noexcept :
		m_WindowWidth(createInfo.m_RuntimeConfig.m_InitialExtent.m_Width),
		m_WindowHeight(createInfo.m_RuntimeConfig.m_InitialExtent.m_Height),
		m_WindowName(createInfo.m_WindowName),
		m_PlatformHost(std::move(createInfo.m_PlatformHost)),
		m_RuntimeConfig(std::move(createInfo.m_RuntimeConfig)),
		m_RuntimePaths(std::move(createInfo.m_RuntimePaths)),
		m_HostServices(std::move(createInfo.m_HostServices)),
		m_ContentRegistration(std::move(createInfo.m_ContentRegistration))
	{
	}

	Application::~Application() noexcept
	{
		Shutdown();
	}

	void Application::Run() noexcept
	{
		if (m_LifecycleState != LifecycleState::Running || !m_PlatformHost)
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

	bool Application::Initialize() noexcept
	{
		if (m_LifecycleState == LifecycleState::Running)
		{
			return true;
		}
		if (m_LifecycleState != LifecycleState::Uninitialized)
		{
			return false;
		}

		m_LifecycleState = LifecycleState::Initializing;

		// Logger
		InitializeLogging();
		if (!m_ContentRegistration.IsValid())
		{
			GGLAB_LOG_ERROR("Application requires valid host-selected content registrations.");
			return FailInitialization();
		}

		m_AppRuntime = std::make_unique<GGLabAppRuntime>();
		const AppRuntimeInitializeResult runtimeInitializeResult = m_AppRuntime->Initialize({
			.m_Config = m_RuntimeConfig,
			.m_Paths = m_RuntimePaths,
			.m_HostServices = m_HostServices,
			});
		if (runtimeInitializeResult != AppRuntimeInitializeResult::Succeeded)
		{
			GGLAB_LOG_ERROR("Failed to initialize the shared app runtime (status={}).",
				static_cast<uint32_t>(runtimeInitializeResult));
			return FailInitialization();
		}

		if (!m_PlatformHost)
		{
			GGLAB_LOG_ERROR("Application requires a platform host.");
			return FailInitialization();
		}

		const PlatformWindowCreateInfo windowCreateInfo{
			.m_Title = m_WindowName,
			.m_Width = m_WindowWidth,
			.m_Height = m_WindowHeight,
		};
		m_PlatformHostInitializationAttempted = true;
		if (!m_PlatformHost->Initialize(windowCreateInfo))
		{
			GGLAB_LOG_ERROR("Failed to initialize the platform host.");
			return FailInitialization();
		}

		auto& mainWindow = m_PlatformHost->GetMainWindow();
		m_WindowWidth = mainWindow.GetWidth();
		m_WindowHeight = mainWindow.GetHeight();

		// InputManager
		m_InputManager = std::make_unique<InputManager>();
		const bool gameInputAvailable = m_InputManager->Initialize(mainWindow.GetNativeHandle());
		if (!gameInputAvailable)
		{
			GGLAB_LOG_WARN(
				"Application will continue without GameInput keyboard and mouse controls.");
		}
		if (m_RuntimeConfig.m_InitialPointerMode == AppRuntimePointerMode::Absolute)
		{
			m_InputManager->GetApplicationInput()->SetPointerMode(AppPointerMode::Absolute);
		}

		const RHIBackendType activeBackend = m_RuntimeConfig.m_RhiBackend ==
			AppRuntimeRHIBackend::DX12 ? RHIBackendType::DX12 : RHIBackendType::Vulkan;
		m_RHIContextFactory =
			Win32RHIContextFactory::Create(activeBackend, mainWindow.GetNativeHandle());
		const AppRuntimeServiceInitializeResult serviceInitializeResult =
			m_AppRuntime->InitializeServices({
				.m_RHIContextFactory = m_RHIContextFactory.get(),
				.m_Input = m_InputManager->GetApplicationInput(),
				.m_ContentRegistration = std::move(m_ContentRegistration),
				.m_WindowWidth = m_WindowWidth,
				.m_WindowHeight = m_WindowHeight,
				});
		if (serviceInitializeResult != AppRuntimeServiceInitializeResult::Succeeded)
		{
			GGLAB_LOG_ERROR("Failed to compose shared runtime services (status={}).",
				static_cast<uint32_t>(serviceInitializeResult));
			return FailInitialization();
		}

		m_Renderer = m_AppRuntime->GetRenderer();
		m_Time = m_AppRuntime->GetTime();
		m_TaskSystem = m_AppRuntime->GetTaskSystem();
		m_AssetManager = m_AppRuntime->GetAssetManager();
		m_EnvironmentAssetController = m_AppRuntime->GetEnvironmentAssetController();
		m_ShaderManager = m_AppRuntime->GetShaderManager();
		m_DemoManager = m_AppRuntime->GetDemoManager();
		m_RenderFrameBuilder = m_AppRuntime->GetRenderFrameBuilder();
		m_DebugDrawSystem = m_AppRuntime->GetDebugDrawSystem();
		const std::optional<uint32_t> labHostIndex = m_AppRuntime->GetLabHostDemoIndex();
		if (labHostIndex)
		{
			m_LabRuntimeLocator =
				std::make_unique<DemoLabRuntimeLocator>(m_DemoManager, *labHostIndex);
		}
		if (m_RuntimeConfig.HasCapability(AppRuntimeCapability::DevelopmentTools))
		{
			m_ApplicationTooling = CreateApplicationToolingIntegration({
				.m_Window = &mainWindow,
				.m_RHIContext = m_Renderer->GetRHIContext(),
				.m_TaskSystem = m_TaskSystem,
				.m_DemoManager = m_DemoManager,
				.m_LabRuntimeLocator = m_LabRuntimeLocator.get(),
				.m_SettingsRoot = m_RuntimePaths.m_SettingsRoot,
				});
			if (!m_ApplicationTooling)
			{
				GGLAB_LOG_WARN("Application will continue without optional development tooling.");
			}
			else
			{
				GGLAB_LOG_INFO("Optional application tooling initialized.");
			}
		}
		else
		{
			GGLAB_LOG_INFO("Optional application tooling omitted by host composition.");
		}

		m_LifecycleState = LifecycleState::Running;
		return true;
	}

	bool Application::Tick() noexcept
	{
		if (m_LifecycleState != LifecycleState::Running)
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
		m_TaskSystem->PumpCompletions({
			.m_MaxCallbacks = 64,
			.m_MaxMilliseconds = 1.0,
			});
		m_AssetManager->DrainLoadCompletions();

		ApplicationInput& input = *m_InputManager->GetApplicationInput();
		if (input.IsKeyPressed(AppInputKey::T))
		{
			input.SetPointerMode(input.GetPointerMode() == AppPointerMode::Absolute
				? AppPointerMode::Relative
				: AppPointerMode::Absolute);
		}

		if (input.IsKeyPressed(AppInputKey::Escape))
		{
			return false;
		}

		const ShaderPreloadStatus shaderPreload = m_ShaderManager->GetPreloadStatus();
		// The bootstrap demo remains active until every shader required by the
		// regular render pipelines has been published on the main thread.
		if (shaderPreload.IsReady() && !m_DemoManager->TickTransitions())
		{
			GGLAB_LOG_ERROR("No active demo is available for rendering.");
			return false;
		}

		// Input routing uses the previous optional tooling frame's capture decision. The
		// new UI frame starts only after the RHI transaction is Ready so a
		// backend can synchronize a swapchain-dependent render contract first.
		const ApplicationToolingInputCapture toolingInputCapture = m_ApplicationTooling
			? m_ApplicationTooling->GetPreviousFrameInputCapture()
			: ApplicationToolingInputCapture{};
		input.SetUICaptureState(toolingInputCapture.m_Keyboard, toolingInputCapture.m_Pointer);

		// Update demo
		auto* demo = m_DemoManager->GetActiveDemo();
		demo->Update();
		m_AssetManager->Tick();
		m_EnvironmentAssetController->Tick();

		auto& world = demo->GetWorld();
		auto& camera = demo->GetCamera();
		auto rendererFrame = m_Renderer->BeginFrame();
		if (!rendererFrame.IsReady())
		{
			return rendererFrame.IsUnavailable();
		}
		ApplicationToolingFrame toolingFrame(m_ApplicationTooling.get());
		// Renderer::Frame may retire RenderGraph resources from its RAII abort path.
		// Keep the graph alive until after the frame has ended.
		RenderGraph rg(m_Renderer->CreateRenderGraphCreateInfo());
		const uint32_t frameSlotIndex = rendererFrame.GetFrameSlotIndex();
		const uint32_t backBufferIndex = rendererFrame.GetBackBufferIndex();

		ShadowVisualizationSettings shadowVisualizationSettings =
			DefaultShadowVisualizationSettings();
		const ViewRenderProfile& authoringViewRenderProfile = demo->GetViewRenderProfile();
		ViewRenderProfile effectiveViewRenderProfile = authoringViewRenderProfile;
		if (m_ApplicationTooling)
		{
			m_ApplicationTooling->ResolveFrameSettings(authoringViewRenderProfile,
				shadowVisualizationSettings, effectiveViewRenderProfile);
		}
		const RenderFrameBuilder::BuildInfo frameBuildInfo{
			.m_World = world,
			.m_CameraRig = demo->GetCameraRig(),
			.m_Renderer = *m_Renderer,
			.m_AssetManager = *m_AssetManager,
			.m_ShadowVisualizationSettings = shadowVisualizationSettings,
			.m_ViewRenderProfile = effectiveViewRenderProfile,
			.m_WindowWidth = m_WindowWidth,
			.m_WindowHeight = m_WindowHeight,
			.m_FrameSlotIndex = frameSlotIndex,
			.m_BackBufferIndex = backBufferIndex,
			.m_FrameSerial = rendererFrame.GetSerial(),
		};
		RenderFrameBuilder::BuildResult frame;
		{
			GGLAB_CPU_PROFILE_SCOPE("RenderFrameBuilder");
			frame = m_RenderFrameBuilder->Build(frameBuildInfo);
		}
		demo->GetCameraRig().SubmitDebugDraw(m_DebugDrawSystem->GetContext());
		frame.m_DebugDrawFrame = m_DebugDrawSystem->SealFrame(frameSlotIndex,
			static_cast<float>(m_Time->GetDeltaTime()), frame.m_DebugDrawCullContext);
		RenderFrameContext renderContext = frame.MakeRenderFrameContext();

		const RenderServices services{
			.m_Renderer = m_Renderer,
			.m_AssetManager = m_AssetManager,
			.m_ShaderManager = m_ShaderManager,
			.m_OverlayExtension = toolingFrame.GetOverlayExtension(),
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
			return false;
		}

		// Draw optional application tooling before Renderer::Render().
		if (toolingFrame.IsOpen())
		{
			GGLAB_CPU_PROFILE_SCOPE("ApplicationTooling");
			std::optional<LoadingProgress> loadingProgress;
			if (!shaderPreload.IsReady())
			{
				loadingProgress = m_AppRuntime->GetStartupLoadingProgress();
			}
			else
			{
				loadingProgress = m_DemoManager->GetLoadingProgress();
			}

			const ApplicationToolingFrameContext toolingContext{
				.m_Camera = &camera,
				.m_CameraController = &demo->GetCameraController(),
				.m_CameraRig = &demo->GetCameraRig(),
				.m_Renderer = m_Renderer,
				.m_World = &world,
				.m_RenderViews = std::span<RenderView>(frame.m_RenderViews),
				.m_RenderQueues = std::span<const RenderQueue>(frame.m_RenderQueues),
				.m_MainRenderView =
					&frame.m_RenderViews[utils::ToIndex(RenderViewID::Main)],
				.m_AssetManager = m_AssetManager,
				.m_EnvironmentAssetController = m_EnvironmentAssetController,
				.m_RenderGraph = &rg,
				.m_DebugDrawSystem = m_DebugDrawSystem,
				.m_DebugDrawFrame = &frame.m_DebugDrawFrame,
				.m_DirectionalShadowSettings =
					frame.m_WorldData.m_MainDirectionalLight.m_ShadowSettings,
				.m_AuthoringViewRenderProfile = &authoringViewRenderProfile,
				.m_EffectiveViewRenderProfile = &effectiveViewRenderProfile,
				.m_LoadingProgress = loadingProgress ? &*loadingProgress : nullptr,
			};
			toolingFrame.Draw(toolingContext);
		}

		// Render
		{
			GGLAB_CPU_PROFILE_SCOPE("RenderGraph Execute");
			m_Renderer->Render(rendererFrame, rg, renderContext);
		}
		RHIFrameEndResult frameEndResult = RHIFrameEndResult::Fatal();
		{
			GGLAB_CPU_PROFILE_SCOPE("Renderer EndFrame");
			frameEndResult = m_Renderer->EndFrame(rendererFrame);
		}
		if (!frameEndResult.IsCompleted())
		{
			return false;
		}

		const DemoFrameFeedback demoFeedback{
			.m_RenderSceneStatus = frame.m_RenderSceneStatus,
			.m_SubmittedFence = frameEndResult.GetSubmittedFence(),
			.m_FrameIndex = m_Time->GetFrameCount(),
			.m_BackBufferIndex = backBufferIndex,
		};
		m_DemoManager->OnFrameSubmitted(demoFeedback);

		// Pipelines without an overlay pass still complete the optional tooling frame.
		toolingFrame.Complete();

		return true;
	}

	bool Application::FailInitialization() noexcept
	{
		if (m_ExitCode == 0)
		{
			m_ExitCode = 1;
		}
		m_LifecycleState = LifecycleState::Failed;
		Shutdown();
		return false;
	}

	void Application::Shutdown() noexcept
	{
		if (m_ShutdownComplete || m_LifecycleState == LifecycleState::ShuttingDown)
		{
			return;
		}

		const bool preserveFailure = m_LifecycleState == LifecycleState::Failed;
		m_LifecycleState = LifecycleState::ShuttingDown;

		// Host-owned tooling and concrete Lab lookup must release their references
		// before the shared runtime destroys Demo/Lab and rendering services.
		m_ApplicationTooling.reset();
		m_LabRuntimeLocator.reset();
		if (m_AppRuntime)
		{
			m_AppRuntime->Shutdown();
		}

		m_Renderer = nullptr;
		m_Time = nullptr;
		m_TaskSystem = nullptr;
		m_AssetManager = nullptr;
		m_EnvironmentAssetController = nullptr;
		m_ShaderManager = nullptr;
		m_DemoManager = nullptr;
		m_RenderFrameBuilder = nullptr;
		m_DebugDrawSystem = nullptr;
		m_RHIContextFactory.reset();

		if (m_InputManager)
		{
			m_InputManager->Finalize();
			m_InputManager.reset();
		}
		if (m_PlatformHost && m_PlatformHostInitializationAttempted)
		{
			m_PlatformHost->Finalize();
			m_PlatformHostInitializationAttempted = false;
		}

		m_ShutdownComplete = true;
		m_LifecycleState = preserveFailure ? LifecycleState::Failed : LifecycleState::Stopped;
	}

	ApplicationInput* Application::GetInput() const noexcept
	{
		return m_InputManager ? m_InputManager->GetApplicationInput() : nullptr;
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
	{
		if (m_InputManager)
		{
			m_InputManager->OnActive();
		}
	}

	void Application::OnInactive() noexcept
	{
		if (m_InputManager)
		{
			m_InputManager->OnInactive();
		}
	}

	void Application::OnSuspend() noexcept
	{
		if (m_IsSuspended)
		{
			return;
		}

		m_IsSuspended = true;
		if (m_InputManager)
		{
			m_InputManager->OnSuspend();
		}

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
		if (m_InputManager)
		{
			m_InputManager->OnResume();
		}

		if (m_Renderer)
		{
			m_Renderer->OnResume();
		}
	}

	void Application::OnResize(uint32_t width, uint32_t height) noexcept
	{
		if (m_LifecycleState != LifecycleState::Running)
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
