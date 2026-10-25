#include "Application/Application.h"
#include "AppRuntimeLog.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Application/ApplicationEnvironment.h"
#include "Application/Capture/ApplicationFrameCapture.h"
#include "Application/Capture/CaptureOnReady.h"
#include "Application/Control/ApplicationControlCaptureJobs.h"
#include "Application/Control/ApplicationControlProtocol.h"
#include "Application/Control/ApplicationControlParameters.h"
#include "Parameters/SessionParameterControl.h"
#include "Application/Platform/Windows/Win32NamedPipeServer.h"
#include "Application/Platform/Windows/Win32Shell.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameSequenceCoordinator.h"
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
#include <vector>

#include <windows.h>

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
		m_SessionId(std::move(createInfo.m_SessionId)),
		m_IdleTimeout(createInfo.m_IdleTimeoutSeconds),
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
				GGLAB_LOG_INFO_ALWAYS("Exit requested by closing the main window.");
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
			.m_HostEnvironment = MakeApplicationHostEnvironment(m_Hidden),
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
		const ShaderTargetProfile activeTargetProfile = activeBackend == RHIBackendType::Vulkan ?
			ShaderTargetProfile::GGLabVulkan13 :
			ShaderTargetProfile::GGLabDX12;
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
			activeTargetProfile == ShaderTargetProfile::GGLabVulkan13 ?
				"gglab-vulkan13" :
				"gglab-dx12");
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
		if (m_AppRuntime->GetFrameCaptureCoordinator())
		{
			m_FrameCapture = std::make_unique<ApplicationFrameCapture>(
				*m_AppRuntime, m_RuntimePaths.m_CaptureRoot,
				ApplicationFrameCapture::OpenDirectoryCallback(m_Hidden ? nullptr : &win32::OpenDirectory));
		}
		if (m_RuntimeConfig.HasCapability(AppRuntimeCapability::DevelopmentTools))
		{
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
			m_ShaderHotReload = std::make_unique<DevelopmentShaderHotReloadSystem>(
				DevelopmentShaderHotReloadSystem::CreateInfo{
					.m_BuildRequest = shaderBuildRequest,
					.m_TaskSystem = taskSystem,
					.m_ShaderManager = shaderManager,
					.m_RenderServices = m_AppRuntime->GetRenderServices(),
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
			m_CaptureOnReadyJob = std::make_unique<CaptureOnReady>(CaptureOnReady::CreateInfo{
				.m_Plan = std::move(*m_CaptureOnReady),
				.m_Timeout = m_CaptureTimeout,
				.m_TrySubmit = [this](CapturePlan&& plan)
				{
					return m_AppRuntime->TrySubmitCaptureJob(std::move(plan));
				},
				.m_Cancel = [this](uint64_t id) { return m_AppRuntime->CancelCaptureJob(id); },
				.m_TakeResult = [this](uint64_t id) { return m_AppRuntime->TakeCaptureJobResult(id); },
				});
			m_CaptureOnReadyJob->TryAdmit();
			m_CaptureOnReady.reset();
		}
		m_StartTime = std::chrono::steady_clock::now();
		if (!m_SessionId.empty() && !StartControlSession())
		{
			return FailInitialization();
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
		if (m_CaptureOnReadyJob)
		{
			m_CaptureOnReadyJob->TryAdmit();
		}
		const AppRuntimeTickResult tickResult = m_AppRuntime->Tick({
			.m_ApplicationTooling = m_ApplicationTooling.get(),
			.m_LabRuntimeLocator = m_LabRuntimeLocator.get(),
			});
		if (m_FrameCapture)
		{
			GGLAB_UNUSED(m_FrameCapture->Update());
		}
		if (m_ControlServer && !UpdateControlSession())
		{
			return false;
		}
		if (m_CaptureOnReadyJob && tickResult == AppRuntimeTickResult::Continue &&
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
		const CaptureOnReady::State state = m_CaptureOnReadyJob->Poll();
		if (state == CaptureOnReady::State::Pending)
		{
			return true;
		}
		const auto printLine = [](std::string_view key, std::string_view value) noexcept
			{
				std::fprintf(stdout, "%.*s: %.*s\n", static_cast<int>(key.size()), key.data(),
					static_cast<int>(value.size()), value.data());
			};
		const CaptureAdmission admission = m_CaptureOnReadyJob->GetAdmission();
		printLine("capture-admission", admission.m_Status == CaptureAdmissionStatus::Accepted ?
			"accepted" : admission.m_Status == CaptureAdmissionStatus::Busy ? "busy" : "rejected");
		printLine("capture-admission-reason", ApplicationFrameCapture::GetAdmissionReasonName(admission.m_Reason));
		if (admission.m_JobId != 0)
		{
			printLine("capture-job", std::to_string(admission.m_JobId));
		}
		if (const auto& result = m_CaptureOnReadyJob->GetResult())
		{
			printLine("capture-status", state == CaptureOnReady::State::Completed ? "completed" :
				state == CaptureOnReady::State::Cancelled ? "cancelled" : "failed");
			printLine("capture-publication", ApplicationFrameCapture::GetPublicationName(result->m_Publication));
			printLine("capture-manifest", ApplicationFrameCapture::GetPathText(result->m_ManifestPath));
			if (state == CaptureOnReady::State::Completed)
			{
				// Convenience output for existing one-shot scripts, derived from the job product.
				for (const auto& product : result->m_Products)
				{
					if (product.m_Kind == CaptureProductKind::Image)
					{
						for (const auto& file : product.m_Files)
						{
							printLine(file.extension() == ".png" ? "capture-image" : "capture-metadata",
								ApplicationFrameCapture::GetPathText(file));
						}
					}
				}
				m_ExitCode = 0;
			}
			else
			{
				printLine("capture-failure", ApplicationFrameCapture::GetFailure(*result));
				m_ExitCode = 2;
			}
		}
		else if (state == CaptureOnReady::State::Rejected)
		{
			printLine("capture-status", "failed");
			printLine("capture-failure", ApplicationFrameCapture::GetAdmissionReasonName(admission.m_Reason));
			m_ExitCode = 2;
		}
		else
		{
			printLine("capture-status", "timeout");
			if (const auto cancellation = m_CaptureOnReadyJob->GetCancellation())
			{
				printLine("capture-cancellation", ApplicationFrameCapture::GetCancellationName(*cancellation));
			}
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
				printLine("capture-settled-frames", std::to_string(m_FrameCapture->GetSettledFrameCount()));
			}
			GGLAB_LOG_ERROR_ALWAYS("Capture-on-ready timed out after {} seconds.", m_CaptureTimeout.count());
			m_ExitCode = 3;
		}
		std::fflush(stdout);
		return false;
	}

	bool Application::StartControlSession() noexcept
	{
		std::wstring pipeName = L"\\\\.\\pipe\\gglab-session-";
		pipeName.append(m_SessionId.begin(), m_SessionId.end());
		m_ControlServer = std::make_unique<win32::NamedPipeServer>();
		if (!m_ControlServer->Start(pipeName))
		{
			GGLAB_LOG_ERROR_ALWAYS("Session '{}' could not open its control pipe; the id may "
				"already be in use.", m_SessionId);
			m_ControlServer.reset();
			return false;
		}
		m_ControlCaptureJobs = std::make_unique<ApplicationControlCaptureJobs>(*m_AppRuntime);
		m_LastControlActivity = std::chrono::steady_clock::now();
		GGLAB_LOG_INFO_ALWAYS("Session '{}' accepts control requests (protocol {}).",
			m_SessionId, ApplicationControlProtocolVersion);
		std::fprintf(stdout, "session-ready: %s\n", m_SessionId.c_str());
		std::fflush(stdout);
		return true;
	}

	bool Application::UpdateControlSession() noexcept
	{
		const auto now = std::chrono::steady_clock::now();
		for (const std::shared_ptr<win32::NamedPipeRequest>& pipeRequest : m_ControlServer->Poll())
		{
			m_LastControlActivity = now;
			ApplicationControlParseResult parsed =
				ParseApplicationControlRequest(pipeRequest->GetLine());
			if (!parsed.m_Request)
			{
				pipeRequest->Respond(SerializeApplicationControlError(parsed.m_Id, parsed.m_Error));
				continue;
			}
			HandleControlRequest(pipeRequest, std::move(*parsed.m_Request));
		}
		if (m_StopRequested)
		{
			GGLAB_LOG_INFO_ALWAYS("Session '{}' is stopping on request.", m_SessionId);
			return false;
		}

		// Client polling counts as activity, retained receipts do not. Waiting for
		// content that never loads must not keep an abandoned session alive.
		if (now - m_LastControlActivity > m_IdleTimeout)
		{
			GGLAB_LOG_INFO_ALWAYS("Session '{}' exits after {} idle seconds.", m_SessionId,
				m_IdleTimeout.count());
			return false;
		}
		return true;
	}

	void Application::HandleControlRequest(
		const std::shared_ptr<win32::NamedPipeRequest>& pipeRequest,
		ApplicationControlRequest&& request) noexcept
	{
		const FrameSequenceCoordinator* frameSequence =
			m_AppRuntime ? m_AppRuntime->GetFrameSequenceCoordinator() : nullptr;
		const FrameCaptureCoordinator* frameCapture =
			m_AppRuntime ? m_AppRuntime->GetFrameCaptureCoordinator() : nullptr;
		auto* parameters = m_AppRuntime ? m_AppRuntime->GetParameterControl() : nullptr;
		switch (request.m_Command)
		{
		case ApplicationControlCommand::Status:
			pipeRequest->Respond(SerializeApplicationControlStatus(request.m_Id, {
				.m_SessionId = m_SessionId,
				.m_ProcessId = static_cast<uint32_t>(::GetCurrentProcessId()),
				.m_UptimeSeconds =
					std::chrono::duration<double>(std::chrono::steady_clock::now() - m_StartTime)
					.count(),
				.m_Hidden = m_Hidden,
				.m_Width = m_WindowWidth,
				.m_Height = m_WindowHeight,
				.m_CaptureIntake = m_AppRuntime ? m_AppRuntime->GetCaptureIntakeSnapshot() : CaptureIntakeSnapshot{},
				.m_SettledFrames = m_FrameCapture ? m_FrameCapture->GetSettledFrameCount() : 0,
				.m_Frame = m_FrameCapture ? m_FrameCapture->GetLastFrameState() : nullptr,
				.m_Sequence = frameSequence ? frameSequence->GetStatus() : nullptr,
				.m_NonFiniteInstrumentation = m_AppRuntime && m_AppRuntime->GetFrameCaptureCoordinator()->HasNonFiniteInstrumentation(),
				.m_NonFiniteProduction = m_AppRuntime ? m_AppRuntime->GetFrameCaptureCoordinator()->GetLatestNonFiniteProductionFrame() : nullptr,
				.m_Watch = m_AppRuntime && m_AppRuntime->GetNonFiniteWatchCoordinator() ? &m_AppRuntime->GetNonFiniteWatchCoordinator()->GetStatus() : nullptr,
				.m_Environment = frameCapture ? std::string_view(frameCapture->GetEnvironmentJson()) : std::string_view{},
				.m_ParametersAvailable = parameters && parameters->IsAvailable(),
				.m_ParameterRevision = parameters ? parameters->GetParameterRevision() : 0,
				.m_ParameterBatch = parameters ? parameters->GetParameterBatchStatus() : nullptr,
				.m_LightingProfiles = parameters ? &parameters->GetLightingProfilePublication() : nullptr,
				.m_LightingProfileError = parameters ? parameters->GetLightingProfileError() : std::string{},
				}));
			return;
		case ApplicationControlCommand::Parameters:
			pipeRequest->Respond(SerializeApplicationControlParameters(request.m_Id, m_AppRuntime.get()));
			return;
		case ApplicationControlCommand::Set:
			pipeRequest->Respond(SubmitApplicationControlParameters(request.m_Id,
				request.m_ParameterChanges, m_AppRuntime.get()));
			return;
		case ApplicationControlCommand::ParameterResult:
			pipeRequest->Respond(QueryApplicationControlParameters(request.m_Id,
				request.m_ParameterBatchId, request.m_Consume, m_AppRuntime.get()));
			return;
		case ApplicationControlCommand::Watch:
		{
			auto* watch = m_AppRuntime ? m_AppRuntime->GetNonFiniteWatchCoordinator() : nullptr;
			std::string error;
			if (!watch || !watch->Configure(request.m_Watch, error))
			{
				pipeRequest->Respond(SerializeApplicationControlError(request.m_Id, watch ? error : "Non-finite watch is unavailable."));
				return;
			}
			pipeRequest->Respond(SerializeApplicationControlWatch(request.m_Id, watch->GetStatus(),
				m_AppRuntime->GetFrameCaptureCoordinator()->HasNonFiniteInstrumentation()));
			return;
		}
		case ApplicationControlCommand::Capture:
		case ApplicationControlCommand::Graph:
		case ApplicationControlCommand::Export:
			pipeRequest->Respond(
				m_ControlCaptureJobs->Submit(request.m_Id, std::move(request.m_Capture)));
			return;
		case ApplicationControlCommand::Result:
			pipeRequest->Respond(
				m_ControlCaptureJobs->Query(request.m_Id, request.m_JobId, request.m_Consume));
			return;
		case ApplicationControlCommand::CaptureCancel:
			pipeRequest->Respond(m_ControlCaptureJobs->Cancel(request.m_Id, request.m_JobId));
			return;
		case ApplicationControlCommand::Stop:
			pipeRequest->Respond(SerializeApplicationControlStopping(request.m_Id));
			m_StopRequested = true;
			return;
		case ApplicationControlCommand::Sequence:
		{
			if (!frameSequence)
			{
				pipeRequest->Respond(SerializeApplicationControlError(
					request.m_Id, "Frame sequences are unavailable in this session."));
				return;
			}
			std::string error;
			if (m_AppRuntime->StartFrameSequence(std::move(request.m_Sequence), error) == 0)
			{
				pipeRequest->Respond(SerializeApplicationControlError(request.m_Id, error));
				return;
			}
			pipeRequest->Respond(
				SerializeApplicationControlSequence(request.m_Id, *frameSequence->GetStatus()));
			return;
		}
		case ApplicationControlCommand::SequenceCancel:
		{
			const auto verdict = m_AppRuntime ?
				m_AppRuntime->CancelFrameSequence() :
				FrameSequenceCancelVerdict::NotFound;
			pipeRequest->Respond(SerializeApplicationControlSequenceCancellation(request.m_Id,
				frameSequence ? frameSequence->GetStatus() : nullptr, verdict));
			return;
		}
		}
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
			// Shutdown drains capture work before either facade or runtime is destroyed.
			// Unconsumed job receipts end with this session; they are not cached by WinApp.
			if (m_FrameCapture)
			{
				GGLAB_UNUSED(m_FrameCapture->Update(false));
				m_FrameCapture.reset();
			}
			if (m_CaptureOnReadyJob)
			{
				m_CaptureOnReadyJob->CollectResult();
				m_CaptureOnReadyJob.reset();
			}
			m_ControlCaptureJobs.reset();
			m_AppRuntime.reset();
		}
		if (m_ControlServer)
		{
			m_ControlServer->Stop(SerializeApplicationControlError(
				0, "The session stopped before the request was answered."));
			m_ControlServer.reset();
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
