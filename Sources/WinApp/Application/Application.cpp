#include "Application/Application.h"
#include "AppRuntimeLog.h"
#include "GGLabAppRuntime.h"
#include "Application/Platform/PlatformHost.h"
#include "Application/Platform/PlatformWindow.h"
#include "Application/Platform/Windows/Win32RHIContextFactory.h"
#include "Application/Tooling/ApplicationToolingComposition.h"
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
#include "Application/Shader/DevelopmentShaderBuildBridge.h"
#else
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"
#endif
#include "Application/Demo/DemoLabRuntimeLocator.h"
#include "ApplicationToolingIntegration.h"
#include "ApplicationInput.h"
#include "Core/Input/InputManager.h"
#include "Graphics/Renderer.h"

#include <optional>
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

			if (m_PlatformHost->IsQuitRequested())
			{
				break;
			}
			if (!Tick())
			{
				return;
			}
		}

		m_AppRuntime->HandleHostEvent({
			.m_Type = AppHostEventType::ExitRequested,
			});
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
		ShaderProgramRegistryArtifactRef activeShaderRegistry{};
#if defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
		const ShaderTargetProfile activeTargetProfile = activeBackend == RHIBackendType::Vulkan
			? ShaderTargetProfile::GGLabVulkan13
			: ShaderTargetProfile::GGLabDX12;
		ShaderLooseActiveProgramRegistryReader activeRegistryReader{
			ShaderLooseActiveProgramRegistryLocator(
				m_RuntimePaths.m_ShaderArtifactRoot, activeTargetProfile)
		};
		const ActiveShaderProgramRegistryReadResult activeRegistry = activeRegistryReader.Read();
		if (!activeRegistry.IsSuccess())
		{
			GGLAB_LOG_ERROR(
				"Artifact-only runtime requires a readable packaged active shader registry (status={}).",
				static_cast<uint32_t>(activeRegistry.m_Status));
			return FailInitialization();
		}
		activeShaderRegistry = activeRegistry.m_RegistryRef;
		GGLAB_LOG_INFO("Artifact-only shader startup selected the packaged {} active registry.",
			activeTargetProfile == ShaderTargetProfile::GGLabVulkan13
				? "gglab-vulkan13"
				: "gglab-dx12");
#else
		const DevelopmentShaderBuildRequest shaderBuildRequest{
			.m_ActiveBackend = activeBackend,
			.m_ShaderCompilerPath = m_RuntimePaths.m_RuntimeRoot / "gglab-shaderc.exe",
			.m_ShaderSourceRoot = m_RuntimePaths.m_RuntimeRoot / "Shaders",
			.m_ShaderCacheRoot = m_RuntimePaths.m_RuntimeRoot / "ShaderCache",
			.m_ArtifactRoot = m_RuntimePaths.m_ShaderArtifactRoot,
		};
		const DevelopmentShaderBuildResult shaderArtifacts =
			RunDevelopmentShaderBuild(shaderBuildRequest);
		if (!shaderArtifacts.IsSuccess())
		{
			GGLAB_LOG_ERROR("Failed to prepare development shader artifacts: {}",
				shaderArtifacts.m_Diagnostics);
			return FailInitialization();
		}
		activeShaderRegistry = shaderArtifacts.m_RegistryRef;
#endif
		const AppRuntimeServiceInitializeResult serviceInitializeResult =
			m_AppRuntime->InitializeServices({
				.m_RHIContextFactory = m_RHIContextFactory.get(),
				.m_Input = m_InputManager->GetApplicationInput(),
				.m_ContentRegistration = std::move(m_ContentRegistration),
				.m_WindowWidth = m_WindowWidth,
				.m_WindowHeight = m_WindowHeight,
				.m_ShaderArtifactRoot = m_RuntimePaths.m_ShaderArtifactRoot,
				.m_ActiveShaderRegistry = activeShaderRegistry,
				});
		if (serviceInitializeResult != AppRuntimeServiceInitializeResult::Succeeded)
		{
			GGLAB_LOG_ERROR("Failed to compose shared runtime services (status={}).",
				static_cast<uint32_t>(serviceInitializeResult));
			return FailInitialization();
		}

		Renderer* renderer = m_AppRuntime->GetRenderer();
		TaskSystem* taskSystem = m_AppRuntime->GetTaskSystem();
		ShaderManager* shaderManager = m_AppRuntime->GetShaderManager();
		DemoManager* demoManager = m_AppRuntime->GetDemoManager();
		const std::optional<uint32_t> labHostIndex = m_AppRuntime->GetLabHostDemoIndex();
		if (labHostIndex)
		{
			m_LabRuntimeLocator =
				std::make_unique<DemoLabRuntimeLocator>(demoManager, *labHostIndex);
		}
		if (m_RuntimeConfig.HasCapability(AppRuntimeCapability::DevelopmentTools))
		{
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
			m_ShaderHotReload = std::make_unique<DevelopmentShaderHotReloadSystem>(
				DevelopmentShaderHotReloadSystem::CreateInfo{
					.m_BuildRequest = shaderBuildRequest,
					.m_TaskSystem = taskSystem,
					.m_ShaderManager = shaderManager,
					.m_Renderer = renderer,
				});
			if (!m_ShaderHotReload->Initialize())
			{
				GGLAB_LOG_WARN("Application will continue without shader hot reload.");
				m_ShaderHotReload.reset();
			}
#else
			GGLAB_LOG_INFO(
				"Shader authoring and hot reload are omitted from the artifact-only target.");
#endif
			m_ApplicationTooling = CreateApplicationToolingIntegration({
				.m_Window = &mainWindow,
				.m_RHIContext = renderer->GetRHIContext(),
				.m_TaskSystem = taskSystem,
				.m_DemoManager = demoManager,
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

		if (m_AppRuntime->GetLifecycleState() == AppRuntimeLifecycleState::Suspended)
		{
			m_PlatformHost->WaitForEvents();
			return true;
		}

		m_InputManager->Update();
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
		if (m_ShaderHotReload)
		{
			m_ShaderHotReload->Update();
		}
#endif
		const AppRuntimeTickResult tickResult = m_AppRuntime->Tick({
			.m_ApplicationTooling = m_ApplicationTooling.get(),
			});
		if (tickResult == AppRuntimeTickResult::Suspended)
		{
			m_PlatformHost->WaitForEvents();
			return true;
		}
		return tickResult == AppRuntimeTickResult::Continue;
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
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
		if (m_ShaderHotReload)
		{
			m_ShaderHotReload->Shutdown();
		}
#endif

		if (m_AppRuntime)
		{
			// The runtime owns the GPU-quiescent ordering point, while the host keeps
			// ownership of the concrete tooling integration.
			m_AppRuntime->Shutdown({
				.m_ApplicationTooling = m_ApplicationTooling.get(),
				});
			m_AppRuntime.reset();
		}
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
		m_ShaderHotReload.reset();
#endif
		// PrepareForShutdown has retired all borrowed runtime/GPU resources. The
		// inactive host objects can now be destroyed without touching dead services.
		m_ApplicationTooling.reset();
		m_LabRuntimeLocator.reset();

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

	void Application::HandlePlatformEvent(const PlatformEvent& event) noexcept
	{
		switch (event.m_Type)
		{
		case PlatformEventType::Activated:
			if (m_InputManager)
			{
				m_InputManager->OnActive();
			}
			break;
		case PlatformEventType::Deactivated:
			if (m_InputManager)
			{
				m_InputManager->OnInactive();
			}
			break;
		case PlatformEventType::Suspended:
			if (m_AppRuntime->GetLifecycleState() == AppRuntimeLifecycleState::Running)
			{
				if (m_InputManager)
				{
					m_InputManager->OnSuspend();
				}
				m_AppRuntime->HandleHostEvent({
					.m_Type = AppHostEventType::Suspended,
					});
			}
			break;
		case PlatformEventType::Resumed:
			if (m_AppRuntime->GetLifecycleState() == AppRuntimeLifecycleState::Suspended)
			{
				if (m_InputManager)
				{
					m_InputManager->OnResume();
				}
				m_AppRuntime->HandleHostEvent({
					.m_Type = AppHostEventType::Resumed,
					});
			}
			break;
		case PlatformEventType::Resized:
			if (event.m_Width > 0 && event.m_Height > 0 &&
				(event.m_Width != m_WindowWidth || event.m_Height != m_WindowHeight))
			{
				m_WindowWidth = event.m_Width;
				m_WindowHeight = event.m_Height;
				m_AppRuntime->HandleHostEvent({
					.m_Type = AppHostEventType::Resized,
					.m_Width = event.m_Width,
					.m_Height = event.m_Height,
					});
			}
			break;
		}
	}
}
