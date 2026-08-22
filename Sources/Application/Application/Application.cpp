#include "Application/Application.h"
#include "Application/ApplicationLog.h"
#include "Application/Platform/PlatformHost.h"
#include "Application/Platform/PlatformWindow.h"
#include "Application/Demo/DemoLabHost.h"
#include "Application/Demo/DemoLabRuntimeLocator.h"
#include "Application/Demo/DemoLoadingShell.h"
#include "Application/Demo/DemoManager.h"
#include "Application/Demo/DemoPlayground.h"
#include "Application/Demo/StartDemo.h"
#include "Application/Demo/DemoTypes.h"
#include "Application/Lab/Sessions/CullingLabSession.h"
#include "Application/LoadingProgress.h"
#include "ApplicationInput.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "Core/Time.h"
#include "Core/Profiling/CpuProfiler.h"
#include "Core/Input/InputManager.h"
#include "Graphics/Renderer.h"
#include "Graphics/Asset/Streaming/AssetUploadScheduler.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/EnvironmentAssetController.h"
#include "Graphics/CameraRig.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/RenderFrameBuilder.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/DebugDraw/DebugDrawSystem.h"
#include "Diagnostics/Builders/LabSnapshotProvider.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/LoadingOverlay.h"
#include "DevTools/DevelopGui/DevelopGuiSystem.h"
#include "DevTools/DevelopGui/Panels/DemoPanel.h"
#include "DevTools/DevelopGui/Panels/LabPanel.h"

#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] RHIBackendType ToRHIBackendType(AppRuntimeRHIBackend backend) noexcept
		{
			switch (backend)
			{
			case AppRuntimeRHIBackend::DX12:
				return RHIBackendType::DX12;
			case AppRuntimeRHIBackend::Vulkan:
				return RHIBackendType::Vulkan;
			default:
				return RHIBackendType::Unknown;
			}
		}
	}

	Application::Application(CreateInfo createInfo) noexcept :
		m_WindowWidth(createInfo.m_RuntimeConfig.m_InitialExtent.m_Width),
		m_WindowHeight(createInfo.m_RuntimeConfig.m_InitialExtent.m_Height),
		m_WindowName(createInfo.m_WindowName),
		m_PlatformHost(std::move(createInfo.m_PlatformHost)),
		m_RuntimeConfig(std::move(createInfo.m_RuntimeConfig)),
		m_RuntimePaths(std::move(createInfo.m_RuntimePaths)),
		m_HostServices(std::move(createInfo.m_HostServices))
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
		if (!m_RuntimeConfig.IsValid())
		{
			GGLAB_LOG_ERROR("Application requires a valid host-supplied runtime config.");
			return FailInitialization();
		}
		if (!m_RuntimePaths.IsValid())
		{
			GGLAB_LOG_ERROR("Application requires valid absolute host-supplied runtime paths.");
			return FailInitialization();
		}
		if (!m_RuntimeConfig.HasCapability(AppRuntimeCapability::BuiltInContent))
		{
			GGLAB_LOG_ERROR("Current Application composition requires built-in content capability.");
			return FailInitialization();
		}
		m_TaskSystem = std::make_unique<TaskSystem>(TaskSystem::CreateInfo{
			.m_WorkerLifecycle = m_HostServices.m_TaskWorkerLifecycle,
			});

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

		const RHIBackendType activeBackend = ToRHIBackendType(m_RuntimeConfig.m_RhiBackend);

		// Time
		m_Time = std::make_unique<Time>();
		m_Time->Initialize();

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

		// ShaderManager
		m_ShaderManager = std::make_unique<ShaderManager>(
			activeBackend, m_RuntimePaths.m_ShaderSourceRoot, m_RuntimePaths.m_ShaderCacheRoot);
		InitializeAssets();

		// Renderer
		m_Renderer = std::make_unique<Renderer>();
		Renderer::CreateInfo rendererCreateInfo{};
		rendererCreateInfo.m_Backend = activeBackend;
		rendererCreateInfo.m_ShaderManager = m_ShaderManager.get();
		rendererCreateInfo.m_TaskSystem = m_TaskSystem.get();
		rendererCreateInfo.m_IblDerivedDataCacheDirectory =
			m_RuntimePaths.m_IblDerivedDataRoot;
		rendererCreateInfo.m_ShaderSourceRoot = m_RuntimePaths.m_ShaderSourceRoot;
		rendererCreateInfo.m_NativeWindowHandle = mainWindow.GetNativeHandle();
		rendererCreateInfo.m_Width = m_WindowWidth;
		rendererCreateInfo.m_Height = m_WindowHeight;
		rendererCreateInfo.m_AdapterSelector = m_RuntimeConfig.m_AdapterSelector;
		rendererCreateInfo.m_EnableDebugValidation =
			m_RuntimeConfig.m_RequestRuntimeValidation;
		if (!m_Renderer->Initialize(rendererCreateInfo))
		{
			GGLAB_LOG_ERROR("Failed to initialize the renderer.");
			return FailInitialization();
		}

		m_DebugDrawSystem = std::make_unique<DebugDrawSystem>(DebugDrawSystem::CreateInfo{
			.m_Device = m_Renderer->GetDevice(),
			.m_FrameSlotCount = m_Renderer->GetRHIContext()->GetFrameSlotCount(),
			});

		AssetManager::CreateInfo assetManagerCreateInfo{};
		assetManagerCreateInfo.m_Device = m_Renderer->GetDevice();
		assetManagerCreateInfo.m_TaskSystem = m_TaskSystem.get();
		assetManagerCreateInfo.m_TransferManager = m_Renderer->GetTransferManager();
		assetManagerCreateInfo.m_AssetUploadScheduler = m_Renderer->GetAssetUploadScheduler();
		assetManagerCreateInfo.m_SamplerRegistry = m_Renderer->GetSamplerRegistry();
		assetManagerCreateInfo.m_TextureDerivedDataCacheDirectory =
			m_RuntimePaths.m_TextureDerivedDataRoot;
		assetManagerCreateInfo.m_AssetRoot = m_RuntimePaths.m_AssetRoot;
		m_AssetManager = std::make_unique<AssetManager>(assetManagerCreateInfo);
		m_Renderer->GetIBLBakeScheduler()->AttachAssetManager(*m_AssetManager);
		const LabId startupLab = m_RuntimeConfig.m_StartupLabId
			? LabId(*m_RuntimeConfig.m_StartupLabId)
			: CullingLabSession::GetId();
		m_EnvironmentAssetController =
			std::make_unique<EnvironmentAssetController>(EnvironmentAssetController::CreateInfo{
				.m_AssetManager = m_AssetManager.get(),
				.m_EnvironmentLighting = m_Renderer->GetEnvironmentLightingSystem(),
				.m_AssetRoot = m_RuntimePaths.m_AssetRoot,
				});
		m_EnvironmentAssetController->Initialize(m_RuntimePaths.m_EnvironmentAssetRoot);

		m_DemoManager = std::make_unique<DemoManager>(m_Renderer.get());
		m_DemoManager->OnResize(m_WindowWidth, m_WindowHeight);

		const DemoCreateInfo demoCreateInfo{
			.m_Services =
				{
					.m_Renderer = m_Renderer.get(),
					.m_AssetManager = m_AssetManager.get(),
					.m_ShaderManager = m_ShaderManager.get(),
					.m_TaskSystem = m_TaskSystem.get(),
					.m_Input = m_InputManager->GetApplicationInput(),
					.m_Time = m_Time.get(),
					.m_DebugDraw = &m_DebugDrawSystem->GetContext(),
					.m_EnvironmentAssetController = m_EnvironmentAssetController.get(),
				},
			.m_WindowWidth = m_WindowWidth,
			.m_WindowHeight = m_WindowHeight,
		};
		m_DemoManager->SetBootstrapDemo(std::make_unique<DemoLoadingShell>(demoCreateInfo));
		const uint32_t startIndex = m_DemoManager->RegisterDemo("Demo.Start",
			[demoCreateInfo]() noexcept -> std::unique_ptr<DemoBase>
			{ return std::make_unique<StartDemo>(demoCreateInfo); });
		const uint32_t playgroundIndex = m_DemoManager->RegisterDemo("Demo.Playground",
			[demoCreateInfo]() noexcept -> std::unique_ptr<DemoBase>
			{ return std::make_unique<DemoPlayground>(demoCreateInfo); });
		const uint32_t labHostIndex = m_DemoManager->RegisterDemo("Demo.LabHost",
			[demoCreateInfo, startupLab]() noexcept -> std::unique_ptr<DemoBase>
			{ return std::make_unique<DemoLabHost>(demoCreateInfo, startupLab); });
		if (startIndex >= m_DemoManager->GetDemoCount() ||
			playgroundIndex >= m_DemoManager->GetDemoCount() ||
			labHostIndex >= m_DemoManager->GetDemoCount())
		{
			GGLAB_LOG_ERROR("Failed to register startup demos.");
			return FailInitialization();
		}
		uint32_t startupDemoIndex = startIndex;
		std::string_view startupDemoName = "Demo.Start";
		switch (m_RuntimeConfig.m_StartupDemo)
		{
		case AppRuntimeStartupDemo::Playground:
			startupDemoIndex = playgroundIndex;
			startupDemoName = "Demo.Playground";
			break;
		case AppRuntimeStartupDemo::LabHost:
			startupDemoIndex = labHostIndex;
			startupDemoName = "Demo.LabHost";
			break;
		case AppRuntimeStartupDemo::Start:
		default:
			break;
		}
		m_DemoManager->RequestActiveDemo(startupDemoIndex);
		m_LabRuntimeLocator =
			std::make_unique<DemoLabRuntimeLocator>(m_DemoManager.get(), labHostIndex);
		GGLAB_LOG_INFO("Startup configuration: demo='{}', lab='{}', mouse_mode='{}'.",
			startupDemoName, startupLab.GetName(),
			m_RuntimeConfig.m_InitialPointerMode == AppRuntimePointerMode::Absolute
				? "absolute"
				: "relative");
		GGLAB_LOG_INFO(
			"Runtime paths: assets='{}', shaders='{}', shader_cache='{}', ibl_ddc='{}', texture_ddc='{}'.",
			m_RuntimePaths.m_AssetRoot.string(), m_RuntimePaths.m_ShaderSourceRoot.string(),
			m_RuntimePaths.m_ShaderCacheRoot.string(),
			m_RuntimePaths.m_IblDerivedDataRoot.string(),
			m_RuntimePaths.m_TextureDerivedDataRoot.string());

		if (m_RuntimeConfig.HasCapability(AppRuntimeCapability::DevelopmentTools))
		{
			m_DevelopGuiSystem = std::make_unique<DevelopGuiSystem>();
			const DevelopGuiSystem::CreateInfo developGuiCreateInfo{
				.m_Window = &mainWindow,
				.m_RHIContext = m_Renderer->GetRHIContext(),
				.m_SettingsPath = m_RuntimePaths.m_SettingsRoot / "imgui.ini",
			};
			if (!m_DevelopGuiSystem->Initialize(developGuiCreateInfo))
			{
				GGLAB_LOG_WARN("Application will continue without DevelopGui.");
				m_DevelopGuiSystem.reset();
			}
			else
			{
				m_DevelopGuiSystem->GetDevToolsRuntime().SetTaskSystem(m_TaskSystem.get());
				m_DevelopGuiSystem->GetDevToolsRuntime().GetDiagnostics().RegisterProvider(
					std::make_unique<LabSnapshotProvider>(
						[runtimeLocator = m_LabRuntimeLocator.get()]() noexcept
							-> const LabSnapshotSourceBase*
						{
							return runtimeLocator
								? runtimeLocator->GetLabRuntimeIfCreated()
								: nullptr;
						}),
					SnapshotUpdatePolicy::EveryFrame);
				m_DevelopGuiSystem->GetDevToolsRuntime().GetRegistry().RegisterPanel(
					std::make_unique<DemoPanel>(m_DemoManager.get()));
				m_DevelopGuiSystem->GetDevToolsRuntime().GetRegistry().RegisterPanel(
					std::make_unique<LabPanel>(m_LabRuntimeLocator.get()));
			}
		}

		m_RenderFrameBuilder = std::make_unique<RenderFrameBuilder>();

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

		// Input routing uses the previous ImGui frame's capture decision. The
		// new UI frame starts only after the RHI transaction is Ready so a
		// backend can synchronize a swapchain-dependent render contract first.
		input.SetUICaptureState(
			m_DevelopGuiSystem && m_DevelopGuiSystem->WantsKeyboardCapture(),
			m_DevelopGuiSystem && m_DevelopGuiSystem->WantsMouseCapture());

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
		const bool developGuiFrameOpen =
			m_DevelopGuiSystem && m_DevelopGuiSystem->BeginFrame();
		// Renderer::Frame may retire RenderGraph resources from its RAII abort path.
		// Keep the graph alive until after the frame has ended.
		RenderGraph rg(m_Renderer->CreateRenderGraphCreateInfo());
		const uint32_t frameSlotIndex = rendererFrame.GetFrameSlotIndex();
		const uint32_t backBufferIndex = rendererFrame.GetBackBufferIndex();

		ShadowVisualizationSettings shadowVisualizationSettings = m_DevelopGuiSystem
			? m_DevelopGuiSystem->GetDevToolsRuntime().GetRenderVisualizationSettings().m_Shadow
			: DefaultShadowVisualizationSettings();
		const ViewRenderProfile& authoringViewRenderProfile = demo->GetViewRenderProfile();
		const ViewRenderProfile effectiveViewRenderProfile = m_DevelopGuiSystem
			? m_DevelopGuiSystem->GetDevToolsRuntime().ResolveViewRenderProfile(
				authoringViewRenderProfile)
			: authoringViewRenderProfile;
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
			.m_Renderer = m_Renderer.get(),
			.m_AssetManager = m_AssetManager.get(),
			.m_ShaderManager = m_ShaderManager.get(),
			.m_OverlayExtension = m_DevelopGuiSystem.get(),
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
			guiContext.m_CameraRig = &demo->GetCameraRig();
			guiContext.m_Renderer = m_Renderer.get();
			guiContext.m_World = &world;
			guiContext.m_RenderViews = std::span<RenderView>(frame.m_RenderViews);
			guiContext.m_RenderQueues = std::span<const RenderQueue>(frame.m_RenderQueues);
			guiContext.m_MainRenderView = &frame.m_RenderViews[utils::ToIndex(RenderViewID::Main)];
			guiContext.m_AuthoringViewRenderProfile = &authoringViewRenderProfile;
			guiContext.m_EffectiveViewRenderProfile = &effectiveViewRenderProfile;
			guiContext.m_AssetManager = m_AssetManager.get();
			guiContext.m_EnvironmentAssetController = m_EnvironmentAssetController.get();
			guiContext.m_RenderGraph = &rg;
			guiContext.m_DirectionalShadowSettings =
				frame.m_WorldData.m_MainDirectionalLight.m_ShadowSettings;
			guiContext.m_DebugDrawSystem = m_DebugDrawSystem.get();
			guiContext.m_DebugDrawFrame = frame.m_DebugDrawFrame;

			m_DevelopGuiSystem->Draw(guiContext);
			if (!shaderPreload.IsReady())
			{
				DrawLoadingOverlay(GetStartupLoadingProgress());
			}
			else if (const auto loadingProgress = m_DemoManager->GetLoadingProgress())
			{
				DrawLoadingOverlay(*loadingProgress);
			}
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
			if (m_DevelopGuiSystem && m_DevelopGuiSystem->IsFrameOpen())
			{
				m_DevelopGuiSystem->EndFrame();
			}
			return false;
		}

		const DemoFrameFeedback demoFeedback{
			.m_RenderSceneStatus = frame.m_RenderSceneStatus,
			.m_SubmittedFence = frameEndResult.GetSubmittedFence(),
			.m_FrameIndex = m_Time->GetFrameCount(),
			.m_BackBufferIndex = backBufferIndex,
		};
		m_DemoManager->OnFrameSubmitted(demoFeedback);

		// Pipelines without a DevelopGui render pass must still close the ImGui frame.
		if (m_DevelopGuiSystem && m_DevelopGuiSystem->IsFrameOpen())
		{
			m_DevelopGuiSystem->EndFrame();
		}

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

		if (m_AssetManager)
		{
			// Close public submission before client OnExit hooks release their interests.
			m_AssetManager->BeginShutdown();

			// Release active and pending demo/Lab asset interests while their services
			// are still alive. GPU-facing session objects remain alive until WaitIdle.
			if (m_DemoManager)
			{
				m_DemoManager->PrepareForAssetShutdown();
			}

			// Commit the pinned fallback before releasing environment texture leases.
			m_EnvironmentAssetController.reset();

			// Stop workers and deliver terminal completion notifications while task
			// consumers are still alive.
			if (m_TaskSystem)
			{
				m_TaskSystem->Shutdown();
				m_TaskSystem->PumpCompletions();
				m_AssetManager->DrainLoadCompletions();
			}

			GGLAB_ASSERT_MSG(m_Renderer && m_Renderer->IsInitialized(),
				"Application asset lifetime requires an initialized renderer.");
			if (m_Renderer && m_Renderer->IsInitialized())
			{
				// Task completions only enqueue CPU-ready streaming payloads. Drain CPU-ready
				// and upload-ready work before waiting for the uploads submitted by that work.
				m_Renderer->GetAssetUploadScheduler()->DrainReadyWork();

				// Must flush here for gpu resource safe release next
				m_Renderer->GetRHIContext()->WaitIdle();
				m_Renderer->GetAssetUploadScheduler()->Finalize();

				m_RenderFrameBuilder.reset();
				if (m_DevelopGuiSystem)
				{
					m_DevelopGuiSystem->Finalize();
					m_DevelopGuiSystem.reset();
				}
				m_LabRuntimeLocator.reset();
				m_DemoManager.reset();
				m_DebugDrawSystem.reset();
				m_Renderer->GetIBLBakeScheduler()->DetachAssetManager();
				m_AssetManager->PrepareForShutdown(m_Renderer->GetLastSubmittedFencePoint());
			}
			m_AssetManager.reset();
		}
		else if (m_TaskSystem)
		{
			m_TaskSystem->Shutdown();
		}

		m_RenderFrameBuilder.reset();
		if (m_DevelopGuiSystem)
		{
			m_DevelopGuiSystem->Finalize();
			m_DevelopGuiSystem.reset();
		}
		m_LabRuntimeLocator.reset();
		m_DemoManager.reset();
		m_DebugDrawSystem.reset();
		if (m_Renderer)
		{
			m_Renderer->Finalize();
			m_Renderer.reset();
		}

		m_EnvironmentAssetController.reset();
		m_ShaderManager.reset();
		if (m_InputManager)
		{
			m_InputManager->Finalize();
			m_InputManager.reset();
		}
		m_Time.reset();
		m_TaskSystem.reset();

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

	void Application::InitializeAssets() noexcept
	{
		// Shader preload
		{
			std::vector<ShaderDesc> shaderDescs;
			const auto addShader =
				[&shaderDescs](const wchar_t* sourcePath, ShaderStage stage, const wchar_t* entry)
				{
					shaderDescs.push_back({
						.m_SourcePath = sourcePath,
						.m_Stage = stage,
						.m_Entry = entry,
						});
				};
			const auto addGraphicsShader = [&addShader](const wchar_t* sourcePath)
				{
					addShader(sourcePath, ShaderStage::Vertex, L"VSMain");
					addShader(sourcePath, ShaderStage::Pixel, L"PSMain");
				};

			addShader(L"Passes/PassForwardCoverage.hlsl", ShaderStage::Vertex, L"VSMain");
			addShader(L"Passes/PassForwardPBR.hlsl", ShaderStage::Pixel, L"PSMain");
			addShader(L"Passes/PassDepthPrepass.hlsl", ShaderStage::Pixel, L"PSAlphaTest");
			addShader(L"Passes/PassForwardPlusCull.hlsl", ShaderStage::Compute, L"CSMain");
			addGraphicsShader(L"Passes/PassDirectionalShadowMap.hlsl");
			addGraphicsShader(L"Passes/PassShadowMapPreview.hlsl");
			addGraphicsShader(L"Passes/PassFinalColor.hlsl");
			addGraphicsShader(L"Passes/PassBloom.hlsl");
			addGraphicsShader(L"Passes/PassPostProcessPreview.hlsl");
			addGraphicsShader(L"Passes/PassDebugDraw.hlsl");
			addGraphicsShader(L"Passes/PassSkybox.hlsl");
			addGraphicsShader(L"Passes/PassIBLEnvironment.hlsl");
			addGraphicsShader(L"Passes/PassIBLEnvironmentMip.hlsl");
			addGraphicsShader(L"Passes/PassIBLIrradiance.hlsl");
			addGraphicsShader(L"Passes/PassIBLPrefilteredSpecular.hlsl");
			addGraphicsShader(L"Passes/PassIBLBrdfLUT.hlsl");
			addGraphicsShader(L"Passes/PassIBLCubemapPreview.hlsl");

			GGLAB_UNUSED(m_ShaderManager->PreloadAsync(
				*m_TaskSystem, std::move(shaderDescs), TaskPriority::Critical));
		}
	}

	LoadingProgress Application::GetStartupLoadingProgress() const noexcept
	{
		const ShaderPreloadStatus status = m_ShaderManager->GetPreloadStatus();
		const float fraction = status.m_TotalCount > 0
			? static_cast<float>(status.m_CompletedCount) /
			static_cast<float>(status.m_TotalCount)
			: 0.0f;
		if (status.HasFailed())
		{
			return {
				.m_Status = LoadingStatus::Failed,
				.m_Fraction = fraction,
				.m_Title = "Starting Graphics Gadget Lab",
				.m_Stage = "Shader preload failed",
				.m_Detail = status.m_Error.empty() ? "The shader preload task was cancelled."
												   : status.m_Error,
			};
		}

		return {
			.m_Status = status.IsReady() ? LoadingStatus::Ready : LoadingStatus::Preparing,
			.m_Fraction = status.IsReady() ? 1.0f : fraction,
			.m_Title = "Starting Graphics Gadget Lab",
			.m_Stage = status.IsReady() ? "Shaders ready" : "Preloading shaders",
			.m_Detail = status.m_CurrentShader.empty() ? "Waiting for a shader worker."
													   : status.m_CurrentShader,
		};
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
