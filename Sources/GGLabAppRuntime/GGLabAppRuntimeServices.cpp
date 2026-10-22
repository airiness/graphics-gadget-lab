#include "GGLabAppRuntime.h"
#include "Parameters/SessionParameterControl.h"

#include "AppRuntimeLog.h"
#include "ApplicationInput.h"
#include "ApplicationToolingIntegration.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/NonFiniteWatchCoordinator.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "Demo/DemoLoadingShell.h"
#include "Demo/DemoManager.h"
#include "Demo/DemoTypes.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsSession.h"
#include "GGLabRuntime/Graphics/DebugDraw/DebugDrawService.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadScheduling.h"
#include "GGLabRuntime/Graphics/EnvironmentAssetController.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "GGLabRuntime/Graphics/Shader/ShaderProgramCatalog.h"
#include "LoadingProgress.h"
#include "GGLabRuntime/Core/Time.h"

#include <cstdlib>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace gglab
{
	GGLabAppRuntime::GGLabAppRuntime() noexcept = default;

	GGLabAppRuntime::~GGLabAppRuntime() noexcept
	{
		Shutdown();
	}

	void GGLabAppRuntime::CollectFrameCaptureResults() noexcept
	{
		if (!m_FrameCapture)
		{
			return;
		}
		if (m_FrameSequence)
		{
			m_FrameSequence->UpdateWatchCoverage();
			m_FrameSequence->UpdateCaptureJobs([this](uint64_t jobId)
			{
				return m_FrameCapture->ConsumeJobReceipt(jobId);
			});
		}
		// One owner establishes host visibility after internal result routing. The
		// reserved receipt remains in the coordinator until explicit host consumption.
		m_FrameCapture->PublishAppRuntimeJobResults();
	}

	CaptureAdmission GGLabAppRuntime::TrySubmitCaptureJob(CapturePlan&& plan) noexcept
	{
		if (m_LifecycleState == AppRuntimeLifecycleState::ExitRequested ||
			m_LifecycleState == AppRuntimeLifecycleState::Failed ||
			m_LifecycleState == AppRuntimeLifecycleState::ShuttingDown ||
			m_LifecycleState == AppRuntimeLifecycleState::Stopped)
		{
			return { CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::ShuttingDown };
		}
		if (!m_ServicesInitialized || !m_FrameCapture)
		{
			return { CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::ServicesUnavailable };
		}
		return m_FrameCapture->TrySubmit(std::move(plan));
	}

	CaptureCancelResult GGLabAppRuntime::CancelCaptureJob(uint64_t jobId) noexcept
	{
		return m_FrameCapture ? m_FrameCapture->CancelJob(jobId) : CaptureCancelResult::NotFound;
	}

	std::optional<CaptureJobSnapshot> GGLabAppRuntime::GetCaptureJobSnapshot(uint64_t jobId) const noexcept
	{
		return m_FrameCapture ? m_FrameCapture->GetJobSnapshot(jobId) : std::nullopt;
	}

	CaptureIntakeSnapshot GGLabAppRuntime::GetCaptureIntakeSnapshot() const noexcept
	{
		return m_FrameCapture ?
			m_FrameCapture->GetIntakeSnapshot() :
			CaptureIntakeSnapshot{ .m_Limits = m_CaptureJobLimits };
	}

	const CaptureJobResult* GGLabAppRuntime::FindCaptureJobResult(uint64_t jobId) const noexcept
	{
		return m_FrameCapture ? m_FrameCapture->FindPublishedJobResult(jobId) : nullptr;
	}

	std::optional<CaptureJobResult> GGLabAppRuntime::TakeCaptureJobResult(uint64_t jobId) noexcept
	{
		return m_FrameCapture ? m_FrameCapture->TakePublishedJobResult(jobId) : std::nullopt;
	}

	DiagnosticsView* GGLabAppRuntime::GetDiagnosticsView() const noexcept
	{
		return m_Diagnostics ? m_Diagnostics->GetView() : nullptr;
	}

	DiagnosticsControl* GGLabAppRuntime::GetDiagnosticsControl() const noexcept
	{
		return m_Diagnostics ? m_Diagnostics->GetControl() : nullptr;
	}

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

		// Destroying runtime, tooling or host objects could free memory the GPU still reads.
		// Ending the process without destructors leaves that memory to the operating system.
		[[noreturn]] void AbandonProcessAfterUnverifiedGpuTeardown() noexcept
		{
			GGLAB_LOG_CRITICAL_ALWAYS(
				"App runtime shutdown could not verify GPU idle; the process exits without "
				"destroying GPU-visible resources.");
			FlushLogs();
			std::_Exit(EXIT_FAILURE);
		}
	}

	AppRuntimeServiceInitializeResult GGLabAppRuntime::InitializeServices(
		AppRuntimeServiceCreateInfo createInfo) noexcept
	{
		if (m_ServicesInitialized)
		{
			return AppRuntimeServiceInitializeResult::AlreadyInitialized;
		}
		if (m_LifecycleState != AppRuntimeLifecycleState::Running)
		{
			return AppRuntimeServiceInitializeResult::InvalidState;
		}
		if (!createInfo.IsValid())
		{
			return FailServiceInitialization(
				AppRuntimeServiceInitializeResult::InvalidCreateInfo);
		}
		if (!createInfo.m_ContentRegistration.IsValid())
		{
			return FailServiceInitialization(
				AppRuntimeServiceInitializeResult::InvalidContentRegistration);
		}

		std::optional<std::string_view> startupLabId;
		if (m_Config.m_StartupLabId)
		{
			startupLabId = *m_Config.m_StartupLabId;
		}
		m_ContentRegistration = std::move(createInfo.m_ContentRegistration);
		const ApplicationContentSelection contentSelection = ResolveApplicationContentSelection(
			m_ContentRegistration, m_Config.m_StartupDemoId, startupLabId);
		if (!contentSelection.Succeeded())
		{
			GGLAB_LOG_ERROR("Host-selected startup content is unavailable (status={}).",
				static_cast<uint32_t>(contentSelection.m_Status));
			return FailServiceInitialization(
				AppRuntimeServiceInitializeResult::StartupContentUnavailable);
		}
		if (!m_Config.m_StartupLabParameters.empty() &&
			!contentSelection.m_StartupDemo->m_ProvidesLabRuntime)
		{
			GGLAB_LOG_ERROR("Startup Lab parameters require a startup Demo that hosts Labs.");
			return FailServiceInitialization(
				AppRuntimeServiceInitializeResult::StartupContentUnavailable);
		}

		m_LifecycleState = AppRuntimeLifecycleState::Initializing;
		m_Input = createInfo.m_Input;
		m_WindowWidth = createInfo.m_WindowWidth;
		m_WindowHeight = createInfo.m_WindowHeight;
		const RHIBackendType activeBackend = ToRHIBackendType(m_Config.m_RhiBackend);

		m_Time = std::make_unique<Time>();
		m_Time->Initialize();
		m_Time->SetFixedDeltaTime(m_Config.m_FixedDeltaTimeSeconds);
		m_TaskSystem = std::make_unique<TaskSystem>(TaskSystem::CreateInfo{
			.m_WorkerLifecycle = m_HostServices.m_TaskWorkerLifecycle,
		});
		m_ShaderManager = std::make_unique<ShaderManager>(ShaderManagerCreateInfo{
			.m_ActiveBackend = activeBackend,
			.m_ArtifactRoot = std::move(createInfo.m_ShaderArtifactRoot),
			.m_ActiveRegistry = createInfo.m_ActiveShaderRegistry,
			.m_NonFiniteCounters = m_Config.m_NonFiniteCounters,
		});
		if (!m_ShaderManager->IsReady())
		{
			GGLAB_LOG_ERROR("Failed to initialize ShaderManager (status={}).",
				static_cast<uint32_t>(m_ShaderManager->GetInitializeStatus()));
			return FailServiceInitialization(
				AppRuntimeServiceInitializeResult::ShaderManagerInitializationFailed);
		}
		const AppRuntimeServiceInitializeResult preloadResult =
			BeginInitialShaderPreload(contentSelection);
		if (preloadResult != AppRuntimeServiceInitializeResult::Succeeded)
		{
			return FailServiceInitialization(preloadResult);
		}

		RenderHostInstance renderHostInstance = CreateRenderHost(RenderHostCreateInfo{
			.m_RHIContextFactory = createInfo.m_RHIContextFactory,
			.m_ShaderManager = m_ShaderManager.get(),
			.m_TaskSystem = m_TaskSystem.get(),
			.m_IblDerivedDataCacheDirectory = m_Paths.m_IblDerivedDataRoot,
			.m_Width = m_WindowWidth,
			.m_Height = m_WindowHeight,
			.m_AdapterSelector = m_Config.m_AdapterSelector,
			.m_EnableDebugValidation = m_Config.m_RequestRuntimeValidation,
			.m_EnableGpuBasedValidation = m_Config.m_RequestGpuBasedValidation,
		});
		if (!renderHostInstance.m_Host)
		{
			GGLAB_LOG_ERROR("Failed to initialize the render host.");
			return FailServiceInitialization(
				AppRuntimeServiceInitializeResult::RendererInitializationFailed);
		}
		m_RenderHost = std::move(renderHostInstance.m_Host);
		m_RenderServices = renderHostInstance.m_Services;
		m_RenderComposition = renderHostInstance.m_Composition;
		if (!m_RenderComposition)
		{
			GGLAB_LOG_ERROR("Render host instance is missing its composition access.");
			return FailServiceInitialization(
				AppRuntimeServiceInitializeResult::RendererInitializationFailed);
		}
		m_NonFiniteWatch = std::make_unique<NonFiniteWatchCoordinator>(m_RenderHost->GetFrameCaptureControl());
		m_FrameCapture = std::make_unique<FrameCaptureCoordinator>(
			FrameCaptureCoordinator::CreateInfo{
				.m_Capture = m_RenderHost->GetFrameCaptureControl(),
				.m_DefaultOutputDirectory = m_Paths.m_CaptureRoot,
				.m_ImageEncoder = m_HostServices.m_FrameCaptureImageEncoder,
				.m_NonFiniteWatch = m_NonFiniteWatch.get(),
				.m_JobLimits = m_CaptureJobLimits,
				.m_Environment = {
					.m_Host = m_HostEnvironment,
					.m_Device = m_RenderHost->GetRHIContext()->GetDeviceIdentity(),
					.m_StartedAt = m_StartedAt,
				},
			});
		m_FrameCapture->SetAppRuntimeJobResultOwner();
		m_FrameSequence = std::make_unique<FrameSequenceCoordinator>(*m_FrameCapture);
		m_ParameterControl = std::make_unique<SessionParameterControl>(
			*m_RenderServices.m_Environment, m_RenderServices.m_WorldLighting);
		m_DebugDrawService = CreateDebugDrawService(DebugDrawServiceCreateInfo{
			.m_Device = &m_RenderHost->GetRHIContext()->GetDevice(),
			.m_FrameSlotCount = m_RenderHost->GetRHIContext()->GetFrameSlotCount(),
		});

		AssetManager::CreateInfo assetManagerCreateInfo{};
		assetManagerCreateInfo.m_Device = &m_RenderHost->GetRHIContext()->GetDevice();
		assetManagerCreateInfo.m_TaskSystem = m_TaskSystem.get();
		assetManagerCreateInfo.m_TransferManager = m_RenderComposition->GetTransferManager();
		assetManagerCreateInfo.m_AssetUploadScheduler = m_RenderComposition->GetAssetUploadScheduler();
		assetManagerCreateInfo.m_SamplerRegistry = m_RenderComposition->GetSamplerRegistry();
		assetManagerCreateInfo.m_TextureDerivedDataCacheDirectory =
			m_Paths.m_TextureDerivedDataRoot;
		assetManagerCreateInfo.m_AssetRoot = m_Paths.m_AssetRoot;
		m_AssetManager = std::make_unique<AssetManager>(assetManagerCreateInfo);
		m_RenderComposition->AttachAssetManager(*m_AssetManager);

		m_EnvironmentAssetController =
			std::make_unique<EnvironmentAssetController>(EnvironmentAssetController::CreateInfo{
				.m_AssetManager = m_AssetManager.get(),
				.m_EnvironmentLighting = m_RenderComposition->GetEnvironmentSourceControl(),
				.m_AssetRoot = m_Paths.m_AssetRoot,
			});
		m_EnvironmentAssetController->Initialize(m_Paths.m_EnvironmentAssetRoot);

		m_DemoManager = std::make_unique<DemoManager>(m_RenderHost->GetRHIContext());
		m_DemoManager->OnResize(m_WindowWidth, m_WindowHeight);
		const DemoCreateInfo demoCreateInfo{
			.m_Services =
				{
					.m_RenderServices = m_RenderServices,
					.m_EnvironmentLighting = m_RenderHost->GetEnvironmentLightingView(),
					.m_EnvironmentLightingControl = m_RenderHost->GetEnvironmentLightingControl(),
					.m_IBLCacheControl = m_RenderHost->GetIBLCacheControl(),
					.m_GpuProfiling = m_RenderHost->GetGpuProfilingView(),
					.m_GpuProfilingControl = m_RenderHost->GetGpuProfilingControl(),
					.m_RHIContext = m_RenderHost->GetRHIContext(),
					.m_AssetManager = m_AssetManager.get(),
					.m_ShaderManager = m_ShaderManager.get(),
					.m_TaskSystem = m_TaskSystem.get(),
					.m_Input = m_Input,
					.m_Time = m_Time.get(),
					.m_DebugDraw = &m_DebugDrawService->GetContext(),
					.m_EnvironmentAssetController = m_EnvironmentAssetController.get(),
					.m_AssetRoot = m_Paths.m_AssetRoot,
				},
			.m_WindowWidth = m_WindowWidth,
			.m_WindowHeight = m_WindowHeight,
		};
		m_DemoManager->SetBootstrapDemo(std::make_unique<DemoLoadingShell>(demoCreateInfo));
		const std::span<const LabRegistration> labRegistrations = m_ContentRegistration.m_Labs;
		const LabStartupSelection startupLab{
			.m_Lab = contentSelection.m_StartupLab,
			.m_Parameters = m_Config.m_StartupLabParameters,
		};
		std::optional<uint32_t> startupDemoIndex;
		for (const ApplicationDemoRegistration& registration : m_ContentRegistration.m_Demos)
		{
			const uint32_t registeredIndex = m_DemoManager->RegisterDemo(registration.m_Id,
				[demoCreateInfo, startupLab, labRegistrations, factory = registration.m_Factory]
				() noexcept -> std::unique_ptr<DemoBase>
				{
					return factory(demoCreateInfo, startupLab, labRegistrations);
				});
			if (registeredIndex >= m_DemoManager->GetDemoCount())
			{
				GGLAB_LOG_ERROR("Failed to register demo '{}'.", registration.m_Id);
				return FailServiceInitialization(
					AppRuntimeServiceInitializeResult::InvalidContentRegistration);
			}
			if (&registration == contentSelection.m_StartupDemo)
			{
				startupDemoIndex = registeredIndex;
			}
			if (registration.m_ProvidesLabRuntime)
			{
				m_LabHostDemoIndex = registeredIndex;
			}
		}
		if (!startupDemoIndex)
		{
			GGLAB_LOG_ERROR("Failed to resolve the registered startup demo.");
			return FailServiceInitialization(
				AppRuntimeServiceInitializeResult::StartupContentUnavailable);
		}
		m_DemoManager->RequestActiveDemo(*startupDemoIndex);
		if (m_Config.HasCapability(AppRuntimeCapability::DevelopmentTools))
		{
			m_Diagnostics = CreateDiagnosticsSession(*m_RenderHost, {
				.m_RegisterLabSnapshotProvider = m_LabHostDemoIndex.has_value() });
		}

		GGLAB_LOG_INFO("Startup configuration: demo='{}', lab='{}', mouse_mode='{}'.",
			m_Config.m_StartupDemoId,
			contentSelection.m_StartupLab.IsValid() ? contentSelection.m_StartupLab.GetName() : std::string_view("none"),
			m_Config.m_InitialPointerMode == AppRuntimePointerMode::Absolute ? "absolute" : "relative");
		GGLAB_LOG_INFO(
			"Runtime paths: assets='{}', shader_artifacts='{}', ibl_ddc='{}', texture_ddc='{}'.",
			m_Paths.m_AssetRoot.string(), m_Paths.m_ShaderArtifactRoot.string(),
			m_Paths.m_IblDerivedDataRoot.string(),
			m_Paths.m_TextureDerivedDataRoot.string());

		m_ServicesInitialized = true;
		m_LifecycleState = AppRuntimeLifecycleState::Running;
		return AppRuntimeServiceInitializeResult::Succeeded;
	}

	AppRuntimeServiceInitializeResult GGLabAppRuntime::FailServiceInitialization(
		AppRuntimeServiceInitializeResult result) noexcept
	{
		m_LifecycleState = AppRuntimeLifecycleState::Failed;
		Shutdown();
		return result;
	}

	AppRuntimeServiceInitializeResult GGLabAppRuntime::BeginInitialShaderPreload(
		const ApplicationContentSelection& contentSelection) noexcept
	{
		ShaderProgramDemandSet demands;
		if (!demands.AddRange(shader_programs::GetRendererInitialShaderProgramDemand()) ||
			!AppendSelectedContentShaderProgramDemand(contentSelection, demands))
		{
			return AppRuntimeServiceInitializeResult::InvalidContentRegistration;
		}
		std::vector<ShaderProgramRef> shaderDemandSnapshot = demands.ReleasePrograms();
		// Reject incompatible registries before creating a render host. Otherwise a missing
		// interface key only fails the async job and leaves the bootstrap shell running.
		for (const auto& program : shaderDemandSnapshot)
		{
			if (!m_ShaderManager->ResolveArtifact(program))
			{
				GGLAB_LOG_ERROR(
					"Startup program registry has no binding for {}::{} (stage={}, counters={}).",
					program.m_ProgramId, program.m_VariantId, static_cast<uint32_t>(program.m_Stage),
					m_ShaderManager->HasNonFiniteCounters());
				return AppRuntimeServiceInitializeResult::StartupShaderProgramsUnavailable;
			}
		}
		GGLAB_UNUSED(m_ShaderManager->PreloadAsync(
			*m_TaskSystem, std::move(shaderDemandSnapshot), TaskPriority::Critical));
		return AppRuntimeServiceInitializeResult::Succeeded;
	}

	LoadingProgress GGLabAppRuntime::GetStartupLoadingProgress() const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ShaderManager.get());
		const ShaderPreloadStatus status = m_ShaderManager->GetPreloadStatus();
		const float fraction = status.m_TotalCount > 0 ?
			static_cast<float>(status.m_CompletedCount) /
				static_cast<float>(status.m_TotalCount) :
			0.0f;
		if (status.HasFailed())
		{
			return {
				.m_Status = LoadingStatus::Failed,
				.m_Fraction = fraction,
				.m_Title = "Starting Graphics Gadget Lab",
				.m_Stage = "Shader preload failed",
				.m_Detail = status.m_Error.empty() ?
					"The shader preload task was cancelled." :
					status.m_Error,
			};
		}

		return {
			.m_Status = status.IsReady() ? LoadingStatus::Ready : LoadingStatus::Preparing,
			.m_Fraction = status.IsReady() ? 1.0f : fraction,
			.m_Title = "Starting Graphics Gadget Lab",
			.m_Stage = status.IsReady() ? "Shaders ready" : "Preloading shaders",
			.m_Detail = status.m_CurrentShader.empty() ?
				"Waiting for a shader worker." :
				status.m_CurrentShader,
		};
	}

	void GGLabAppRuntime::Shutdown(AppRuntimeShutdownInfo shutdownInfo) noexcept
	{
		if (m_ShutdownComplete || m_LifecycleState == AppRuntimeLifecycleState::ShuttingDown)
		{
			return;
		}

		if (m_ParameterControl)
		{
			m_ParameterControl->Finish();
		}
		const bool preserveFailure = m_LifecycleState == AppRuntimeLifecycleState::Failed;
		m_LifecycleState = AppRuntimeLifecycleState::ShuttingDown;
		if (m_FrameCapture)
		{
			m_FrameCapture->Update();
			CollectFrameCaptureResults();
		}
		// Sequence jobs cancel delivery as a whole; claimed writes keep their real
		// publication outcome. Runtime and writer owners drain during shutdown.
		if (m_FrameSequence)
		{
			GGLAB_UNUSED(m_FrameSequence->Cancel());
		}
		if (m_FrameCapture)
		{
			m_FrameCapture->PrepareForShutdown();
			CollectFrameCaptureResults();
		}
		bool applicationToolingPrepared = false;
		const auto prepareApplicationTooling = [&]() noexcept
			{
				if (!applicationToolingPrepared && shutdownInfo.m_ApplicationTooling)
				{
					shutdownInfo.m_ApplicationTooling->PrepareForShutdown();
					applicationToolingPrepared = true;
				}
			};

		if (m_AssetManager)
		{
			m_AssetManager->BeginShutdown();
			if (m_DemoManager)
			{
				m_DemoManager->PrepareForAssetShutdown();
			}
			m_EnvironmentAssetController.reset();

			if (m_TaskSystem)
			{
				m_TaskSystem->Shutdown();
				m_TaskSystem->PumpCompletions();
				m_AssetManager->DrainLoadCompletions();
			}

			GGLAB_ASSERT_MSG(m_RenderHost && m_RenderHost->IsInitialized(),
				"App runtime asset lifetime requires an initialized render host.");
			if (m_RenderHost && m_RenderHost->IsInitialized())
			{
				RHIContext& context = *m_RenderHost->GetRHIContext();
				if (context.IsPoisoned() || context.IsDeviceLost())
				{
					m_RenderComposition->GetAssetUploadScheduler()->StopGpuWork();
				}
				else
				{
					m_RenderComposition->GetAssetUploadScheduler()->DrainReadyWork();
				}
				// Verified idle, or confirmed device loss, lets every owner below release.
				const RHITeardownState teardown = QuiesceRHIForTeardown(context);
				if (teardown == RHITeardownState::Unverified)
				{
					AbandonProcessAfterUnverifiedGpuTeardown();
				}
				if (teardown == RHITeardownState::DeviceLost)
				{
					m_RenderComposition->GetAssetUploadScheduler()->StopGpuWork();
				}
				// Host-owned tooling may retain backend pipelines, descriptors and user
				// textures referenced by the last submitted frame. Retire them only at
				// this quiescent point, while all borrowed runtime services remain alive.
				prepareApplicationTooling();
				m_RenderComposition->GetAssetUploadScheduler()->Finalize();

				m_Diagnostics.reset();
				m_DemoManager.reset();
				m_DebugDrawService.reset();
				m_RenderComposition->DetachAssetManager();
				m_AssetManager->PrepareForShutdown(m_RenderComposition->GetLastSubmittedFencePoint());
			}
			m_AssetManager.reset();
		}
		else if (m_TaskSystem)
		{
			m_TaskSystem->Shutdown();
		}

		// Lifecycle-only and partial-initialization runtimes have no GPU work to
		// quiesce, but still honor the tooling shutdown contract exactly once.
		prepareApplicationTooling();
		m_Diagnostics.reset();
		m_DemoManager.reset();
		m_DebugDrawService.reset();
		if (m_RenderHost)
		{
			m_RenderHost->Finalize();
			// Finalize published every outstanding Runtime capture result; drain
			// them while the render host still owns the capture control.
			if (m_FrameCapture)
			{
				m_FrameCapture->FinalizeAfterRenderHost();
				CollectFrameCaptureResults();
			}
			m_RenderHost.reset();
		}
		m_RenderComposition = nullptr;
		m_EnvironmentAssetController.reset();
		m_ShaderManager.reset();
		m_Time.reset();
		m_TaskSystem.reset();
		m_Input = nullptr;
		m_LabHostDemoIndex.reset();
		m_ServicesInitialized = false;
		m_ResizePending = false;

		m_ShutdownComplete = true;
		m_LifecycleState = preserveFailure ?
			AppRuntimeLifecycleState::Failed :
			AppRuntimeLifecycleState::Stopped;
	}
}
