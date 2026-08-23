#include "GGLabAppRuntime.h"

#include "AppRuntimeLog.h"
#include "ApplicationInput.h"
#include "Demo/DemoLoadingShell.h"
#include "Demo/DemoManager.h"
#include "Demo/DemoTypes.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/Asset/Streaming/AssetUploadScheduler.h"
#include "Graphics/DebugDraw/DebugDrawSystem.h"
#include "Graphics/EnvironmentAssetController.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/RenderFrameBuilder.h"
#include "Graphics/Renderer.h"
#include "Graphics/Shader/ShaderManager.h"
#include "LoadingProgress.h"
#include "Core/Time.h"

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

		[[nodiscard]] std::vector<ShaderDesc> BuildInitialShaderDemandSnapshot()
		{
			constexpr size_t initialShaderDemandCount = 30;
			std::vector<ShaderDesc> shaderDescs;
			shaderDescs.reserve(initialShaderDemandCount);
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
			GGLAB_ASSERT_MSG(shaderDescs.size() == initialShaderDemandCount,
				"Initial shader demand must preserve the validated 30-entry startup baseline.");
			return shaderDescs;
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

		m_LifecycleState = AppRuntimeLifecycleState::Initializing;
		m_Input = createInfo.m_Input;
		m_WindowWidth = createInfo.m_WindowWidth;
		m_WindowHeight = createInfo.m_WindowHeight;
		const RHIBackendType activeBackend = ToRHIBackendType(m_Config.m_RhiBackend);

		m_Time = std::make_unique<Time>();
		m_Time->Initialize();
		m_TaskSystem = std::make_unique<TaskSystem>(TaskSystem::CreateInfo{
			.m_WorkerLifecycle = m_HostServices.m_TaskWorkerLifecycle,
			});
		m_ShaderManager = std::make_unique<ShaderManager>(
			activeBackend, m_Paths.m_ShaderSourceRoot, m_Paths.m_ShaderCacheRoot);
		BeginInitialShaderPreload();

		m_Renderer = std::make_unique<Renderer>();
		Renderer::CreateInfo rendererCreateInfo{};
		rendererCreateInfo.m_RHIContextFactory = createInfo.m_RHIContextFactory;
		rendererCreateInfo.m_ShaderManager = m_ShaderManager.get();
		rendererCreateInfo.m_TaskSystem = m_TaskSystem.get();
		rendererCreateInfo.m_IblDerivedDataCacheDirectory = m_Paths.m_IblDerivedDataRoot;
		rendererCreateInfo.m_ShaderSourceRoot = m_Paths.m_ShaderSourceRoot;
		rendererCreateInfo.m_Width = m_WindowWidth;
		rendererCreateInfo.m_Height = m_WindowHeight;
		rendererCreateInfo.m_AdapterSelector = m_Config.m_AdapterSelector;
		rendererCreateInfo.m_EnableDebugValidation = m_Config.m_RequestRuntimeValidation;
		if (!m_Renderer->Initialize(rendererCreateInfo))
		{
			GGLAB_LOG_ERROR("Failed to initialize the renderer.");
			return FailServiceInitialization(
				AppRuntimeServiceInitializeResult::RendererInitializationFailed);
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
			m_Paths.m_TextureDerivedDataRoot;
		assetManagerCreateInfo.m_AssetRoot = m_Paths.m_AssetRoot;
		m_AssetManager = std::make_unique<AssetManager>(assetManagerCreateInfo);
		m_Renderer->GetIBLBakeScheduler()->AttachAssetManager(*m_AssetManager);

		m_EnvironmentAssetController =
			std::make_unique<EnvironmentAssetController>(EnvironmentAssetController::CreateInfo{
				.m_AssetManager = m_AssetManager.get(),
				.m_EnvironmentLighting = m_Renderer->GetEnvironmentLightingSystem(),
				.m_AssetRoot = m_Paths.m_AssetRoot,
				});
		m_EnvironmentAssetController->Initialize(m_Paths.m_EnvironmentAssetRoot);

		m_DemoManager = std::make_unique<DemoManager>(m_Renderer.get());
		m_DemoManager->OnResize(m_WindowWidth, m_WindowHeight);
		const DemoCreateInfo demoCreateInfo{
			.m_Services =
				{
					.m_Renderer = m_Renderer.get(),
					.m_AssetManager = m_AssetManager.get(),
					.m_ShaderManager = m_ShaderManager.get(),
					.m_TaskSystem = m_TaskSystem.get(),
					.m_Input = m_Input,
					.m_Time = m_Time.get(),
					.m_DebugDraw = &m_DebugDrawSystem->GetContext(),
					.m_EnvironmentAssetController = m_EnvironmentAssetController.get(),
				},
			.m_WindowWidth = m_WindowWidth,
			.m_WindowHeight = m_WindowHeight,
		};
		m_DemoManager->SetBootstrapDemo(std::make_unique<DemoLoadingShell>(demoCreateInfo));
		const std::span<const LabRegistration> labRegistrations = m_ContentRegistration.m_Labs;
		std::optional<uint32_t> startupDemoIndex;
		for (const ApplicationDemoRegistration& registration : m_ContentRegistration.m_Demos)
		{
			const uint32_t registeredIndex = m_DemoManager->RegisterDemo(registration.m_Id,
				[demoCreateInfo, startupLab = contentSelection.m_StartupLab, labRegistrations,
					factory = registration.m_Factory]() noexcept -> std::unique_ptr<DemoBase>
				{ return factory(demoCreateInfo, startupLab, labRegistrations); });
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
		m_RenderFrameBuilder = std::make_unique<RenderFrameBuilder>();

		GGLAB_LOG_INFO("Startup configuration: demo='{}', lab='{}', mouse_mode='{}'.",
			m_Config.m_StartupDemoId,
			contentSelection.m_StartupLab.IsValid()
				? contentSelection.m_StartupLab.GetName()
				: std::string_view("none"),
			m_Config.m_InitialPointerMode == AppRuntimePointerMode::Absolute
				? "absolute"
				: "relative");
		GGLAB_LOG_INFO(
			"Runtime paths: assets='{}', shaders='{}', shader_cache='{}', ibl_ddc='{}', texture_ddc='{}'.",
			m_Paths.m_AssetRoot.string(), m_Paths.m_ShaderSourceRoot.string(),
			m_Paths.m_ShaderCacheRoot.string(), m_Paths.m_IblDerivedDataRoot.string(),
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

	void GGLabAppRuntime::BeginInitialShaderPreload() noexcept
	{
		// Freeze the complete initial runtime demand before starting eager preload.
		// Loading progress observes this same ShaderManager preload operation.
		std::vector<ShaderDesc> shaderDemandSnapshot = BuildInitialShaderDemandSnapshot();
		GGLAB_UNUSED(m_ShaderManager->PreloadAsync(
			*m_TaskSystem, std::move(shaderDemandSnapshot), TaskPriority::Critical));
	}

	LoadingProgress GGLabAppRuntime::GetStartupLoadingProgress() const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ShaderManager.get());
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

	void GGLabAppRuntime::Shutdown() noexcept
	{
		if (m_ShutdownComplete || m_LifecycleState == AppRuntimeLifecycleState::ShuttingDown)
		{
			return;
		}

		const bool preserveFailure = m_LifecycleState == AppRuntimeLifecycleState::Failed;
		m_LifecycleState = AppRuntimeLifecycleState::ShuttingDown;

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

			GGLAB_ASSERT_MSG(m_Renderer && m_Renderer->IsInitialized(),
				"App runtime asset lifetime requires an initialized renderer.");
			if (m_Renderer && m_Renderer->IsInitialized())
			{
				m_Renderer->GetAssetUploadScheduler()->DrainReadyWork();
				m_Renderer->GetRHIContext()->WaitIdle();
				m_Renderer->GetAssetUploadScheduler()->Finalize();

				m_RenderFrameBuilder.reset();
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
		m_DemoManager.reset();
		m_DebugDrawSystem.reset();
		if (m_Renderer)
		{
			m_Renderer->Finalize();
			m_Renderer.reset();
		}
		m_EnvironmentAssetController.reset();
		m_ShaderManager.reset();
		m_Time.reset();
		m_TaskSystem.reset();
		m_Input = nullptr;
		m_LabHostDemoIndex.reset();
		m_ServicesInitialized = false;
		m_ResizePending = false;

		m_ShutdownComplete = true;
		m_LifecycleState = preserveFailure
			? AppRuntimeLifecycleState::Failed
			: AppRuntimeLifecycleState::Stopped;
	}
}
