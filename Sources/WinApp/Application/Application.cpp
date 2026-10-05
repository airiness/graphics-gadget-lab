#include "Application/Application.h"
#include "AppRuntimeLog.h"
#include "Application/Capture/ApplicationFrameCapture.h"
#include "Capture/FrameCaptureCoordinator.h"
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
#include "Application/Platform/Windows/Input/InputManager.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "Lab/LabRuntime.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
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
		m_ContentRegistration(std::move(createInfo.m_ContentRegistration)),
		m_CaptureOnReady(std::move(createInfo.m_CaptureOnReady)),
		m_CaptureTimeout(createInfo.m_CaptureTimeoutSeconds),
		m_Hidden(createInfo.m_Hidden)
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
			.m_Hidden = m_Hidden,
		};
		m_PlatformHostInitializationAttempted = true;
		if (!m_PlatformHost->Initialize(windowCreateInfo))
		{
			GGLAB_LOG_ERROR("Failed to initialize the platform host.");
			return FailInitialization();
		}

		auto& mainWindow = m_PlatformHost->GetMainWindow();
		if (m_Hidden &&
			(mainWindow.GetWidth() != m_WindowWidth || mainWindow.GetHeight() != m_WindowHeight))
		{
			// A capture must never silently change resolution.
			GGLAB_LOG_ERROR_ALWAYS(
				"The hidden window client size {}x{} differs from the requested {}x{}.",
				mainWindow.GetWidth(), mainWindow.GetHeight(), m_WindowWidth, m_WindowHeight);
			return FailInitialization();
		}
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
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
		const DevelopmentShaderBuildRequest shaderBuildRequest{
			.m_ActiveBackend = activeBackend,
			.m_ShaderCompilerPath = m_RuntimePaths.m_RuntimeRoot / "gglab-shaderc.exe",
			.m_ShaderSourceRoot = m_RuntimePaths.m_RuntimeRoot / "Shaders",
			.m_ShaderCacheRoot = m_RuntimePaths.m_ShaderArtifactRoot.parent_path() / "ShaderCache",
			.m_ArtifactRoot = m_RuntimePaths.m_ShaderArtifactRoot,
		};
#endif
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

		RenderHost* renderHost = m_AppRuntime->GetRenderHost();
		TaskSystem* taskSystem = m_AppRuntime->GetTaskSystem();
		ShaderManager* shaderManager = m_AppRuntime->GetShaderManager();
		DemoManager* demoManager = m_AppRuntime->GetDemoManager();
		const std::optional<uint32_t> labHostIndex = m_AppRuntime->GetLabHostDemoIndex();
		if (labHostIndex)
		{
			m_LabRuntimeLocator =
				std::make_unique<DemoLabRuntimeLocator>(demoManager, *labHostIndex);
		}
		if (FrameCaptureCoordinator* frameCapture = m_AppRuntime->GetFrameCaptureCoordinator())
		{
			m_FrameCapture = std::make_unique<ApplicationFrameCapture>(
				*frameCapture, m_RuntimePaths.m_CaptureRoot);
		}
		if (m_RuntimeConfig.HasCapability(AppRuntimeCapability::DevelopmentTools))
		{
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
				m_ShaderHotReload = std::make_unique<DevelopmentShaderHotReloadSystem>(
				DevelopmentShaderHotReloadSystem::CreateInfo{
					.m_BuildRequest = shaderBuildRequest,
					.m_TaskSystem = taskSystem,
					.m_ShaderManager = shaderManager,
					.m_RenderTemporal = m_AppRuntime->GetRenderServices().m_Temporal,
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
				.m_RHIContext = renderHost->GetRHIContext(),
				.m_DemoManager = demoManager,
				.m_LabRuntimeLocator = m_LabRuntimeLocator.get(),
				.m_FrameCapture = m_FrameCapture.get(),
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

		if (m_CaptureOnReady)
		{
			if (!m_FrameCapture)
			{
				GGLAB_LOG_ERROR_ALWAYS("Capture-on-ready requires the frame capture service.");
				return FailInitialization();
			}
			m_CaptureOnReadyRequestId = m_FrameCapture->Submit(std::move(*m_CaptureOnReady));
			m_CaptureOnReady.reset();
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

		if (m_Hidden)
		{
			PaceHiddenFrame();
		}
		// GameInput reports devices regardless of window focus. Reading it only
		// while the main window is active keeps keys meant for another
		// application, such as Escape, from reaching the runtime; deactivation
		// already reset the published input state.
		if (!m_Hidden && m_IsWindowActive)
		{
			m_InputManager->Update();
		}
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
		if (m_ShaderHotReload)
		{
			m_ShaderHotReload->Update();
		}
#endif
		const ApplicationInput* input = m_AppRuntime->GetInput();
		if (m_FrameCapture && m_IsWindowActive && input &&
			!input->IsKeyboardCapturedByUI() && input->IsKeyPressed(AppInputKey::F9))
		{
			m_FrameCapture->Capture();
		}
		const AppRuntimeTickResult tickResult = m_AppRuntime->Tick({
			.m_ApplicationTooling = m_ApplicationTooling.get(),
			.m_LabRuntimeLocator = m_LabRuntimeLocator.get(),
			});
		if (m_FrameCapture)
		{
			m_FrameCapture->Update();
		}
		if (m_CaptureOnReadyRequestId != 0 && tickResult == AppRuntimeTickResult::Continue &&
			!UpdateCaptureOnReady())
		{
			return false;
		}
		if (tickResult == AppRuntimeTickResult::Suspended)
		{
			m_PlatformHost->WaitForEvents();
			return true;
		}
		if (tickResult == AppRuntimeTickResult::Fatal)
		{
			// Host fatal policy: record the failure and exit without resuming the
			// failed runtime. Shutdown still runs the GPU-quiescent ordering point.
			GGLAB_LOG_CRITICAL_ALWAYS("Application is exiting after a fatal runtime failure.");
			if (m_ExitCode == 0)
			{
				m_ExitCode = 1;
			}
			m_LifecycleState = LifecycleState::Failed;
			return false;
		}
		return tickResult == AppRuntimeTickResult::Continue;
	}

	void Application::PaceHiddenFrame() noexcept
	{
		// Hidden frames present to no visible surface, so presentation does not
		// throttle them; cap them at 60 frames per second.
		constexpr std::chrono::microseconds frameInterval{ 16667 };
		const auto now = std::chrono::steady_clock::now();
		if (m_NextHiddenFrameTime > now)
		{
			std::this_thread::sleep_until(m_NextHiddenFrameTime);
		}
		m_NextHiddenFrameTime = std::max(m_NextHiddenFrameTime, now) + frameInterval;
	}

	bool Application::UpdateCaptureOnReady() noexcept
	{
		const auto now = std::chrono::steady_clock::now();
		if (!m_CaptureDeadline)
		{
			m_CaptureDeadline =
				now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(m_CaptureTimeout);
		}

		// Machine-readable outcome lines on stdout for unattended callers.
		const auto printLine = [](std::string_view key, std::string_view value) noexcept
			{
				std::fprintf(stdout, "%.*s: %.*s\n", static_cast<int>(key.size()), key.data(),
					static_cast<int>(value.size()), value.data());
			};
		const auto toUtf8 = [](const std::filesystem::path& path)
			{
				const std::u8string text = path.u8string();
				return std::string(text.begin(), text.end());
			};

		if (const FrameCaptureRequestResult* result =
			m_FrameCapture->FindResult(m_CaptureOnReadyRequestId))
		{
			if (result->m_Status == FrameCaptureRequestStatus::Completed)
			{
				printLine("capture-status", "completed");
				printLine("capture-image", toUtf8(result->m_ImagePath));
				printLine("capture-metadata", toUtf8(result->m_MetadataPath));
				m_ExitCode = 0;
			}
			else
			{
				printLine("capture-status", result->m_Status == FrameCaptureRequestStatus::Cancelled
					? "cancelled" : "failed");
				printLine("capture-failure", result->m_Failure);
				m_ExitCode = 2;
			}
			std::fflush(stdout);
			m_CaptureOnReadyRequestId = 0;
			return false;
		}
		if (now < *m_CaptureDeadline)
		{
			return true;
		}

		m_FrameCapture->Cancel(m_CaptureOnReadyRequestId);
		printLine("capture-status", "timeout");
		if (const FrameCaptureFrameState* frameState = m_FrameCapture->GetLastFrameState())
		{
			for (const FrameCaptureGate& gate : frameState->m_Readiness.m_Gates)
			{
				if (gate.m_State != FrameCaptureGateState::Ready)
				{
					printLine("capture-pending-gate", std::format("{} ({}): {}", gate.m_Name,
						GetFrameCaptureGateStateName(gate.m_State), gate.m_Detail));
				}
			}
			printLine("capture-settled-frames",
				std::to_string(m_FrameCapture->GetSettledFrameCount()));
		}
		std::fflush(stdout);
		GGLAB_LOG_ERROR_ALWAYS("Capture-on-ready timed out after {} seconds.",
			m_CaptureTimeout.count());
		m_ExitCode = 3;
		m_CaptureOnReadyRequestId = 0;
		return false;
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
			// Shutdown finished every capture request; log the final results before
			// the coordinator is destroyed with the runtime.
			if (m_FrameCapture)
			{
				m_FrameCapture->Update();
				m_FrameCapture.reset();
			}
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
			m_IsWindowActive = true;
			if (m_InputManager)
			{
				m_InputManager->OnActive();
			}
			break;
		case PlatformEventType::Deactivated:
			m_IsWindowActive = false;
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
