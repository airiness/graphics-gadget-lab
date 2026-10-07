#include "GGLabAppRuntime.h"

#include "AppRuntimeLog.h"
#include "ApplicationInput.h"
#include "ApplicationToolingIntegration.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "GGLabRuntime/Core/Profiling/CpuProfiler.h"
#include "GGLabRuntime/Core/Time.h"
#include "Demo/DemoBase.h"
#include "Demo/DemoManager.h"
#include "Demo/DemoTypes.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsSession.h"
#include "GGLabRuntime/Diagnostics/RuntimeToolingAdapters.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadScheduling.h"
#include "GGLabRuntime/Graphics/IBLBakeTypes.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/CameraReferenceView.h"
#include "GGLabRuntime/Graphics/CameraRig.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessColorState.h"
#include "GGLabRuntime/Graphics/DebugDraw/DebugDrawService.h"
#include "GGLabRuntime/Graphics/EnvironmentAssetController.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingControlBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingViewBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "Lab/LabInterfaces.h"
#include "Lab/LabRuntime.h"
#include "LoadingProgress.h"

#include <array>
#include <chrono>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		class ScopedDiagnosticsFrame final
		{
		public:
			ScopedDiagnosticsFrame(
				DiagnosticsSession* session, const DiagnosticsFrameContext& context) noexcept :
				m_Session(session)
			{
				if (m_Session)
				{
					m_Session->BeginFrame(context);
				}
			}
			ScopedDiagnosticsFrame(const ScopedDiagnosticsFrame&) = delete;
			ScopedDiagnosticsFrame& operator=(const ScopedDiagnosticsFrame&) = delete;
			~ScopedDiagnosticsFrame() noexcept
			{
				if (m_Session)
				{
					m_Session->EndFrame();
				}
			}

			[[nodiscard]] DiagnosticsView* GetView() const noexcept
			{
				return m_Session ? m_Session->GetView() : nullptr;
			}
			[[nodiscard]] DiagnosticsControl* GetControl() const noexcept
			{
				return m_Session ? m_Session->GetControl() : nullptr;
			}

		private:
			DiagnosticsSession* m_Session = nullptr;
		};

		[[nodiscard]] std::string_view GetBackendName(AppRuntimeRHIBackend backend) noexcept
		{
			switch (backend)
			{
			case AppRuntimeRHIBackend::DX12:
				return "dx12";
			case AppRuntimeRHIBackend::Vulkan:
				return "vulkan";
			case AppRuntimeRHIBackend::Unknown:
				break;
			}
			return "unknown";
		}

		struct CaptureFrameInputs
		{
			const DemoManager& m_DemoManager;
			const DemoBase& m_Demo;
			const EnvironmentAssetController& m_EnvironmentAssetController;
			const RenderServices& m_RenderServices;
			const Time& m_Time;
			const ShaderPreloadStatus& m_ShaderPreload;
			const CameraRig::CameraSlot& m_DisplayCameraSlot;
			std::span<const CameraReferenceView> m_ReferenceViews;
			RenderViewID m_DisplayViewId = RenderViewID::Main;
			uint64_t m_TemporalSessionIdentity = 0;
			const ResolvedTemporalFramePlan& m_TemporalFramePlan;
			const TemporalFrameTransaction& m_TemporalFrameTransaction;
			const TemporalAASettings& m_TemporalSettings;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			AppRuntimeRHIBackend m_Backend = AppRuntimeRHIBackend::Unknown;
			bool m_DevelopmentTools = false;
		};

		// Runtime-owned readiness gates first, then the content-owned ones.
		[[nodiscard]] FrameCaptureReadiness BuildCaptureReadiness(
			const CaptureFrameInputs& inputs) noexcept
		{
			FrameCaptureReadiness readiness;
			const ShaderPreloadStatus& shaders = inputs.m_ShaderPreload;
			readiness.Add("shaders",
				shaders.IsReady() ? FrameCaptureGateState::Ready
				: shaders.HasFailed() ? FrameCaptureGateState::Failed
				: FrameCaptureGateState::Pending,
				shaders.HasFailed() ? shaders.m_Error
				: shaders.IsReady() ? std::string{}
				: std::format("{} of {} shaders preloaded.", shaders.m_CompletedCount,
					shaders.m_TotalCount));

			if (inputs.m_DemoManager.HasPendingActiveDemo())
			{
				const std::optional<LoadingProgress> progress =
					inputs.m_DemoManager.GetLoadingProgress();
				readiness.Add("content-transition", FrameCaptureGateState::Pending,
					progress ? progress->m_Title : std::string("Switching Demos."));
			}
			else
			{
				readiness.Add("content-transition", FrameCaptureGateState::Ready);
			}
			inputs.m_Demo.AppendCaptureReadiness(readiness);

			const bool environmentPending =
				inputs.m_EnvironmentAssetController.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex;
			readiness.Add("environment",
				environmentPending ? FrameCaptureGateState::Pending : FrameCaptureGateState::Ready,
				environmentPending ? "Loading the selected environment." : "");

			// The published IBL generation matches the latest request once the
			// requested environment lighting is fully baked or cache-restored.
			const IBLBakeStatus& bake = inputs.m_RenderServices.m_Environment->GetBakingStatus();
			const bool iblPending = bake.m_RequestedGeneration != bake.m_ActiveGeneration;
			readiness.Add("ibl",
				iblPending ? FrameCaptureGateState::Pending : FrameCaptureGateState::Ready,
				iblPending ? std::format("Baking {} ({:.0f}%).", GetIBLBakeStageName(bake.m_Stage),
					bake.m_Progress * 100.0f)
				: std::string{});

			const AssetUploadStatistics uploads =
				inputs.m_RenderServices.m_AssetUpload->GetStatistics();
			const uint32_t pendingUploads = uploads.m_PendingCount +
				uploads.m_CpuPayloadQueue.m_PendingCount +
				uploads.m_ResourcePublicationQueue.m_PendingCount +
				uploads.m_UploadRecordingQueue.m_PendingCount +
				uploads.m_GpuFinalizeQueue.m_PendingCount;
			readiness.Add("asset-uploads",
				pendingUploads > 0 ? FrameCaptureGateState::Pending : FrameCaptureGateState::Ready,
				pendingUploads > 0 ? std::format("{} uploads pending.", pendingUploads)
				: std::string{});
			return readiness;
		}

		[[nodiscard]] FrameCaptureTemporalState BuildCaptureTemporalState(
			const CaptureFrameInputs& inputs) noexcept
		{
			const ResolvedTemporalFramePlan& plan = inputs.m_TemporalFramePlan;
			const TemporalAASettings& settings = inputs.m_TemporalSettings;
			const Vector2& jitter = inputs.m_TemporalFrameTransaction.GetJitterPixels();
			const std::optional<TemporalReferenceSample>& referenceSample =
				inputs.m_TemporalFrameTransaction.GetReferenceSample();
			// One resolution domain until the render/display split lands.
			const std::array<uint32_t, 2> extent{ inputs.m_Width, inputs.m_Height };
			return FrameCaptureTemporalState{
				.m_Requested = plan.m_Requested,
				.m_Status = std::string(GetTemporalAAFrameStatusName(plan.m_Status)),
				.m_DisableReason = std::string(GetTemporalAADisableReasonName(plan.m_DisableReason)),
				.m_SessionIdentity = plan.m_SessionIdentity,
				.m_ResetIdentity = plan.m_ResetIdentity,
				.m_JitterIndex = inputs.m_TemporalFrameTransaction.GetJitterIndex(),
				.m_JitterSequenceLength = plan.m_Active ? temporal::JitterSampleCount
					: referenceSample ? referenceSample->m_Count : 0,
				.m_JitterPixels = { jitter.m_X, jitter.m_Y },
				.m_MaxHistoryFeedback = settings.m_MaxHistoryFeedback,
				.m_DepthAbsoluteThreshold = settings.m_DepthAbsoluteThreshold,
				.m_DepthRelativeThreshold = settings.m_DepthRelativeThreshold,
				.m_VelocityWeightScale = settings.m_VelocityWeightScale,
				.m_LuminanceWeightScale = settings.m_LuminanceWeightScale,
				.m_NeighborhoodClampExpansion = settings.m_NeighborhoodClampExpansion,
				.m_HistoryFilter =
					std::string(GetTemporalAAHistoryFilterName(settings.m_HistoryFilter)),
				.m_CurrentFilter =
					std::string(GetTemporalAACurrentFilterName(settings.m_CurrentFilter)),
				.m_RenderExtent = extent,
				.m_DisplayExtent = extent,
			};
		}

		[[nodiscard]] FrameCaptureFrameState BuildCaptureFrameState(
			const CaptureFrameInputs& inputs) noexcept
		{
			const Camera& camera = *inputs.m_DisplayCameraSlot.m_Camera;
			const auto toArray = [](const Vector3& value) noexcept
				{
					return std::array<float, 3>{ value.m_X, value.m_Y, value.m_Z };
				};
			const uint32_t demoIndex = inputs.m_DemoManager.GetActiveIndex();
			std::vector<std::string> referenceViewIds;
			referenceViewIds.reserve(inputs.m_ReferenceViews.size());
			for (const CameraReferenceView& view : inputs.m_ReferenceViews)
			{
				referenceViewIds.push_back(view.m_Id);
			}
			return FrameCaptureFrameState{
				.m_Backend = std::string(GetBackendName(inputs.m_Backend)),
				// The bootstrap loading Demo has no registered index.
				.m_DemoId = std::string(demoIndex < inputs.m_DemoManager.GetDemoCount()
					? inputs.m_DemoManager.GetDemoName(demoIndex)
					: inputs.m_Demo.GetName()),
				.m_LabId = inputs.m_Demo.GetCaptureContentId(),
				.m_Readiness = BuildCaptureReadiness(inputs),
				.m_SettleKey = {
					.m_TemporalSession = inputs.m_TemporalSessionIdentity,
					.m_CameraResetSerial = camera.GetTemporalResetSerial(),
					.m_DisplayView = static_cast<uint32_t>(inputs.m_DisplayViewId),
					.m_Width = inputs.m_Width,
					.m_Height = inputs.m_Height,
					.m_DemoIndex = demoIndex,
				},
				.m_FrameIndex = inputs.m_Time.GetFrameCount(),
				.m_Camera = {
					.m_Name = inputs.m_DisplayCameraSlot.m_Name,
					.m_Position = toArray(camera.GetPosition()),
					.m_Forward = toArray(camera.GetForward()),
					.m_Up = toArray(camera.GetUp()),
					.m_VerticalFovDegrees = camera.GetFov(),
					.m_NearPlane = camera.GetNear(),
					.m_FarPlane = camera.GetFar(),
				},
				.m_ReferenceViewIds = std::move(referenceViewIds),
				.m_FixedDeltaTime = inputs.m_Time.GetFixedDeltaTime(),
				.m_TotalTime = inputs.m_Time.GetTotalTime(),
				.m_DevelopmentTools = inputs.m_DevelopmentTools,
				.m_Temporal = BuildCaptureTemporalState(inputs),
			};
		}
	}

	AppRuntimeTickResult GGLabAppRuntime::FailRuntime(std::string_view failure) noexcept
	{
		GGLAB_LOG_CRITICAL_ALWAYS("Fatal runtime failure: {}", failure);
		m_LifecycleState = AppRuntimeLifecycleState::Failed;
		return AppRuntimeTickResult::Fatal;
	}

	AppRuntimeTickResult GGLabAppRuntime::Tick(AppRuntimeTickInfo tickInfo) noexcept
	{
		switch (m_LifecycleState)
		{
		case AppRuntimeLifecycleState::Suspended:
			return AppRuntimeTickResult::Suspended;
		case AppRuntimeLifecycleState::Running:
			break;
		case AppRuntimeLifecycleState::Failed:
			return AppRuntimeTickResult::Fatal;
		default:
			return AppRuntimeTickResult::Exit;
		}

		// The first initialization stage remains independently testable. Production
		// frame work starts only after the host has composed the runtime services.
		if (!m_ServicesInitialized)
		{
			return AppRuntimeTickResult::Continue;
		}

		// A sequence capture that the capture writer cannot accept yet defers the
		// whole frame, before time advances, so no extra frame is simulated or
		// rendered while the writer drains.
		if (m_FrameSequence->ShouldDeferFrame())
		{
			m_FrameCapture->Update();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			return AppRuntimeTickResult::Continue;
		}

		GGLAB_CPU_PROFILE_FRAME(m_Time->GetFrameCount() + 1);
		SyncSequenceGpuProfiling();

		if (m_FrameSequence->ShouldHoldTime())
		{
			// Every sample of a supersampled reference frame renders the same instant.
			m_Time->Hold();
		}
		else
		{
			m_Time->Update();
		}
		m_TaskSystem->PumpCompletions({
			.m_MaxCallbacks = 64,
			.m_MaxMilliseconds = 1.0,
			});
		m_AssetManager->DrainLoadCompletions();
		m_FrameCapture->Update();

		if (m_Input->IsKeyPressed(AppInputKey::T))
		{
			m_Input->SetPointerMode(m_Input->GetPointerMode() == AppPointerMode::Absolute
				? AppPointerMode::Relative
				: AppPointerMode::Absolute);
		}

		if (m_Input->IsKeyPressed(AppInputKey::Escape))
		{
			GGLAB_LOG_INFO_ALWAYS("Exit requested by the Escape key.");
			m_LifecycleState = AppRuntimeLifecycleState::ExitRequested;
			return AppRuntimeTickResult::Exit;
		}

		const ShaderPreloadStatus shaderPreload = m_ShaderManager->GetPreloadStatus();
		// The bootstrap demo remains active until every shader required by the
		// regular render pipelines has been published on the main thread.
		if (shaderPreload.IsReady())
		{
			m_DemoManager->BeginTransitionTick();
		}
		if (!tickInfo.m_PreContentUpdate.Run())
		{
			return FailRuntime("Host pre-content update failed.");
		}
		if (shaderPreload.IsReady())
		{
			if (!m_DemoManager->CompleteTransitionTick())
			{
				return FailRuntime("No active demo is available for rendering.");
			}
		}

		ApplicationToolingIntegrationBase* applicationTooling =
			tickInfo.m_ApplicationTooling;
		// Input routing uses the previous optional tooling frame's capture decision. The
		// new UI frame starts only after the RHI transaction is Ready so a
		// backend can synchronize a swapchain-dependent render contract first.
		const ApplicationToolingInputCapture toolingInputCapture = applicationTooling
			? applicationTooling->GetPreviousFrameInputCapture()
			: ApplicationToolingInputCapture{};
		m_Input->SetUICaptureState(
			toolingInputCapture.m_Keyboard, toolingInputCapture.m_Pointer);

		DemoBase* demo = m_DemoManager->GetActiveDemo();
		GGLAB_ASSERT_NOT_NULL(demo);
		demo->Update();
		m_AssetManager->Tick();
		m_EnvironmentAssetController->Tick();

		World& world = demo->GetWorld();
		RenderFrame rendererFrame = m_RenderHost->BeginFrame();
		if (!rendererFrame.IsReady())
		{
			return rendererFrame.IsUnavailable()
				? AppRuntimeTickResult::Continue
				: FailRuntime("The render host failed to begin a frame.");
		}
		ApplicationToolingFrame toolingFrame(applicationTooling);
		RenderServices services = m_RenderServices;
		services.m_TextureAssets = m_AssetManager.get();
		services.m_OverlayExtension = toolingFrame.GetOverlayExtension();
		// The RAII frame handle may retire RenderGraph resources from its abort
		// path. Keep the graph alive until after the frame has ended.
		RenderGraph renderGraph(m_RenderHost->CreateRenderGraphCreateInfo());
		const uint32_t frameSlotIndex = rendererFrame.GetFrameSlotIndex();
		const uint32_t backBufferIndex = rendererFrame.GetBackBufferIndex();

		ShadowVisualizationSettings shadowVisualizationSettings =
			DefaultShadowVisualizationSettings();
		const ViewRenderProfile& authoringViewRenderProfile = demo->GetViewRenderProfile();
		ViewRenderProfile effectiveViewRenderProfile = authoringViewRenderProfile;
		ApplicationToolingFrameSettingsResolution toolingSettingsResolution{};
		if (applicationTooling)
		{
			toolingSettingsResolution = applicationTooling->ResolveFrameSettings(
				authoringViewRenderProfile, shadowVisualizationSettings,
				effectiveViewRenderProfile);
		}
		CameraRig& cameraRig = demo->GetCameraRig();
		// A capture's reference view is restored before the frame is planned, so
		// this frame already renders it.
		if (const std::optional<FrameCaptureViewChange> viewChange =
			m_FrameCapture->GetPendingViewChange())
		{
			m_FrameCapture->OnReferenceViewApplied(viewChange->m_RequestId,
				cameraRig.RestoreReferenceView(viewChange->m_ReferenceViewId));
		}
		// A running sequence poses the main camera after content updates and capture
		// views, so the path alone determines this frame's camera.
		std::optional<TemporalReferenceSample> referenceSample;
		std::optional<FrameSequenceTemporalAAOverrides> sequenceTemporalAAOverrides;
		if (const std::optional<FrameSequencePoseRequest> sequencePose =
			m_FrameSequence->PrepareFrame(
				m_FrameCapture->GetLastFrameState(), cameraRig.GetCameraPaths()))
		{
			m_FrameSequence->OnPoseApplied(cameraRig.ApplyCameraPathFrame(
				sequencePose->m_CameraPathId, sequencePose->m_Frame));
			referenceSample = sequencePose->m_ReferenceSample;
			sequenceTemporalAAOverrides = sequencePose->m_TemporalAAOverrides;
		}
		const CameraRig::EffectiveDisplayView effectiveDisplayView =
			cameraRig.ResolveEffectiveDisplayView();
		GGLAB_ASSERT_MSG(effectiveDisplayView.IsValid(),
			"CameraRig must resolve one effective display view before "
			"frame planning.");
		const CameraRig::CameraSlot* displayCameraSlot = effectiveDisplayView.m_CameraSlot;
		ResolvedViewRenderSettings displayViewSettings =
			ResolveViewRenderSettings(
				effectiveViewRenderProfile, *displayCameraSlot->m_Camera);
		if (referenceSample)
		{
			// The reference owns jitter and accumulation; Temporal AA stays inactive.
			displayViewSettings.m_TemporalAA.m_Enabled = false;
		}
		else if (sequenceTemporalAAOverrides)
		{
			displayViewSettings.m_TemporalAA = ApplyFrameSequenceTemporalAAOverrides(
				*sequenceTemporalAAOverrides, displayViewSettings.m_TemporalAA);
		}
		const uint64_t temporalSessionIdentity =
			(static_cast<uint64_t>(m_DemoManager->GetTemporalSessionSerial()) << 32) |
			static_cast<uint64_t>(demo->GetTemporalSessionSerial());
		RenderPipelineBase& renderPipeline = demo->GetRenderPipeline();
		const ResolvedTemporalFramePlan temporalFramePlan =
			renderPipeline.ResolveTemporalFramePlan({
				.m_Settings = displayViewSettings.m_TemporalAA,
				.m_Capabilities = m_RenderHost->GetTemporalAACapabilityStatus(),
				.m_DisplayViewId = effectiveDisplayView.m_ViewId,
				.m_ResetIdentity = displayCameraSlot->m_Camera->GetTemporalResetSerial(),
				.m_SessionIdentity = temporalSessionIdentity,
				.m_DisplayViewEligible = IsTemporalAADisplayViewEligible(
					effectiveDisplayView.m_ViewId, m_WindowWidth, m_WindowHeight),
			});
		TemporalFrameTransaction& temporalFrameTransaction = m_RenderHost->BeginTemporalFrame(
			rendererFrame, temporalFramePlan, m_WindowWidth, m_WindowHeight,
			displayViewSettings.m_Exposure.m_PreExposure, referenceSample);
		const RenderFrameBuildRequest frameBuildRequest{
			.m_World = world,
			.m_CameraRig = demo->GetCameraRig(),
			.m_ViewRenderProfile = effectiveViewRenderProfile,
			.m_ShadowVisualizationSettings = shadowVisualizationSettings,
			.m_DisplayViewSettings = displayViewSettings,
			.m_TemporalFramePlan = temporalFramePlan,
			.m_TemporalFrameTransaction = temporalFrameTransaction,
			.m_DisplayViewId = effectiveDisplayView.m_ViewId,
			.m_WindowWidth = m_WindowWidth,
			.m_WindowHeight = m_WindowHeight,
			.m_FrameSlotIndex = frameSlotIndex,
			.m_BackBufferIndex = backBufferIndex,
			.m_FrameSerial = rendererFrame.GetSerial(),
		};
		// Captures due this frame, including a sequence frame capture, are issued
		// before its graph binds capture taps.
		FrameCaptureFrameState captureFrameState = BuildCaptureFrameState({
			.m_DemoManager = *m_DemoManager,
			.m_Demo = *demo,
			.m_EnvironmentAssetController = *m_EnvironmentAssetController,
			.m_RenderServices = m_RenderServices,
			.m_Time = *m_Time,
			.m_ShaderPreload = shaderPreload,
			.m_DisplayCameraSlot = *displayCameraSlot,
			.m_ReferenceViews = cameraRig.GetReferenceViews(),
			.m_DisplayViewId = effectiveDisplayView.m_ViewId,
			.m_TemporalSessionIdentity = temporalSessionIdentity,
			.m_TemporalFramePlan = temporalFramePlan,
			.m_TemporalFrameTransaction = temporalFrameTransaction,
			.m_TemporalSettings = displayViewSettings.m_TemporalAA,
			.m_Width = m_WindowWidth,
			.m_Height = m_WindowHeight,
			.m_Backend = m_Config.m_RhiBackend,
			.m_DevelopmentTools = applicationTooling != nullptr,
			});
		m_FrameSequence->BeginFrame(captureFrameState);
		m_FrameCapture->BeginFrame(std::move(captureFrameState));
		RenderFrameBuildResult frame;
		{
			GGLAB_CPU_PROFILE_SCOPE("RenderHostFrameBuilder");
			frame = m_RenderHost->BuildFrame(frameBuildRequest);
		}
		RenderFrameContext validationContext = frame.MakeRenderFrameContext();
		const RenderFrameValidationResult validation =
			renderPipeline.ValidateRenderFrame(validationContext, services);
		if (!validation.IsReady())
		{
			// Nothing has been recorded. The temporal transaction is aborted and the
			// RenderFrame handle retires the begun frame on return.
			m_RenderHost->InvalidateTemporalFrameAfterLateContractFailure(rendererFrame);
			if (validation.m_Status == RenderFrameValidationStatus::Skipped)
			{
				toolingFrame.Complete();
				return AppRuntimeTickResult::Continue;
			}
			toolingFrame.Abort();
			return FailRuntime(std::format(
				"Rendering contract failure: pipeline='{}', frame={}, backend={}, reason='{}'{}{}",
				renderPipeline.GetName(), frame.m_FrameSerial, GetBackendName(m_Config.m_RhiBackend),
				validation.m_Reason, validation.m_Detail.empty() ? "" : ", detail=",
				validation.m_Detail));
		}
		demo->GetCameraRig().SubmitDebugDraw(m_DebugDrawService->GetContext());
		frame.m_DebugDrawFrame = m_DebugDrawService->SealFrame(frameSlotIndex,
			static_cast<float>(m_Time->GetDeltaTime()), frame.m_DebugDrawCullContext);
		RenderFrameContext renderContext = frame.MakeRenderFrameContext();

		{
			GGLAB_CPU_PROFILE_SCOPE("RenderGraph Build");
			renderPipeline.BuildRenderGraph(renderGraph, renderContext, services);
		}
		bool renderGraphCompiled = false;
		{
			GGLAB_CPU_PROFILE_SCOPE("RenderGraph Compile");
			renderGraphCompiled = renderGraph.Compile();
		}
		GGLAB_ASSERT_MSG(renderGraphCompiled, "RenderGraph compilation failed.");
		if (!renderGraphCompiled)
		{
			return FailRuntime("RenderGraph compilation failed.");
		}

		if (toolingFrame.IsOpen())
		{
			GGLAB_CPU_PROFILE_SCOPE("ApplicationTooling");
			const LabRuntime* labRuntime = tickInfo.m_LabRuntimeLocator
				? tickInfo.m_LabRuntimeLocator->GetLabRuntimeIfCreated()
				: nullptr;
			const DiagnosticsFrameContext diagnosticsContext{
				.m_RenderHost = m_RenderHost.get(),
				.m_AssetManager = m_AssetManager.get(),
				.m_EnvironmentAssetController = m_EnvironmentAssetController.get(),
				.m_LabSnapshotSource = labRuntime,
				.m_TaskSystem = m_TaskSystem.get(),
				.m_World = &world,
				.m_RenderGraph = &renderGraph,
				.m_RenderViews = std::span<RenderView>(frame.m_RenderViews),
				.m_RenderQueues = std::span<const RenderQueue>(frame.m_RenderQueues),
				.m_DirectionalShadowFramePlan = &frame.m_DirectionalShadowFramePlan,
				.m_FrameSerial = frame.m_FrameSerial,
				.m_MainRenderView =
					&frame.m_RenderViews[utils::ToIndex(RenderViewID::Main)],
				.m_AuthoringViewRenderProfile = &authoringViewRenderProfile,
				.m_EffectiveViewRenderProfile = &effectiveViewRenderProfile,
				.m_DisplayViewId = frame.m_DisplayViewId,
				.m_DisplayViewSettings =
					&frame.m_ViewRenderSettings[utils::ToIndex(frame.m_DisplayViewId)],
				.m_TemporalFramePlan = &frame.m_TemporalFramePlan,
				.m_GTAOOverrideActive = toolingSettingsResolution.m_GTAOOverrideActive,
			};
			ScopedDiagnosticsFrame diagnosticsFrame(m_Diagnostics.get(), diagnosticsContext);
			std::optional<LoadingProgress> loadingProgress;
			if (!shaderPreload.IsReady())
			{
				loadingProgress = GetStartupLoadingProgress();
			}
			else
			{
				loadingProgress = m_DemoManager->GetLoadingProgress();
			}

			RuntimeToolingAdapters runtimeToolingAdapters(
				world, *m_AssetManager, demo->GetCameraRig());
			const ApplicationToolingFrameContext toolingContext{
				.m_Cameras = &runtimeToolingAdapters.GetCameraView(),
				.m_CameraControl = &runtimeToolingAdapters.GetCameraControl(),
				.m_CameraRenderViewQuery = &demo->GetCameraRig(),
				.m_WorldView = &runtimeToolingAdapters.GetWorldView(),
				.m_WorldControl = &runtimeToolingAdapters.GetWorldControl(),
				.m_DirectionalLight = &runtimeToolingAdapters.GetDirectionalLightView(),
				.m_DirectionalLightControl =
					&runtimeToolingAdapters.GetDirectionalLightControl(),
				.m_AssetControl = &runtimeToolingAdapters.GetAssetControl(),
				.m_EnvironmentSelectionControl = m_EnvironmentAssetController.get(),
				.m_Diagnostics = diagnosticsFrame.GetView(),
				.m_DiagnosticsControl = diagnosticsFrame.GetControl(),
				.m_EnvironmentLighting = m_RenderHost->GetEnvironmentLightingView(),
				.m_EnvironmentLightingControl = m_RenderHost->GetEnvironmentLightingControl(),
				.m_GpuProfiling = m_RenderHost->GetGpuProfilingView(),
				.m_GpuProfilingControl = m_RenderHost->GetGpuProfilingControl(),
				.m_IBLCacheControl = m_RenderHost->GetIBLCacheControl(),
				.m_IBLPreview = m_RenderHost->GetIBLPreviewView(),
				.m_IBLPreviewControl = m_RenderHost->GetIBLPreviewControl(),
				.m_PostProcessPreview = m_RenderHost->GetPostProcessPreviewView(),
				.m_PostProcessPreviewControl = m_RenderHost->GetPostProcessPreviewControl(),
				.m_ShadowPreview = m_RenderHost->GetShadowPreviewView(),
				.m_ShadowPreviewControl = m_RenderHost->GetShadowPreviewControl(),
				.m_DebugDrawChannels = m_DebugDrawService->GetChannelView(),
				.m_DebugDrawChannelControl = m_DebugDrawService->GetChannelControl(),
				.m_DebugDrawFrame = &frame.m_DebugDrawFrame,
				.m_LoadingProgress = loadingProgress ? &*loadingProgress : nullptr,
			};
			toolingFrame.Draw(toolingContext);
		}

		{
			GGLAB_CPU_PROFILE_SCOPE("RenderGraph Execute");
			m_RenderHost->Render(rendererFrame, renderGraph, renderContext);
		}
		RHIFrameEndResult frameEndResult = RHIFrameEndResult::Fatal();
		{
			GGLAB_CPU_PROFILE_SCOPE("RenderHost EndFrame");
			frameEndResult = m_RenderHost->EndFrame(rendererFrame);
		}
		if (!frameEndResult.IsCompleted())
		{
			toolingFrame.Abort();
			return FailRuntime("The render host failed to complete frame submission.");
		}
		m_FrameCapture->OnFrameSubmitted();
		m_FrameSequence->OnFrameSubmitted();
		if (m_FrameSequence->WantsGpuTiming())
		{
			if (const GpuProfilingViewBase* gpuProfiling = m_RenderHost->GetGpuProfilingView())
			{
				m_FrameSequence->OnGpuProfile(gpuProfiling->GetLatestFrame());
			}
		}

		m_DemoManager->OnFrameSubmitted({
			.m_RenderSceneStatus = frame.m_RenderSceneStatus,
			.m_SubmittedFence = frameEndResult.GetSubmittedFence(),
			.m_FrameIndex = m_Time->GetFrameCount(),
			.m_BackBufferIndex = backBufferIndex,
			});

		// Pipelines without an overlay pass still complete the optional tooling frame.
		toolingFrame.Complete();
		return AppRuntimeTickResult::Continue;
	}

	void GGLabAppRuntime::SyncSequenceGpuProfiling() noexcept
	{
		GpuProfilingControlBase* control = m_RenderHost->GetGpuProfilingControl();
		const GpuProfilingViewBase* view = m_RenderHost->GetGpuProfilingView();
		if (!control || !view)
		{
			return;
		}
		const bool timingWanted = m_FrameSequence->WantsGpuTiming();
		if (timingWanted && !m_SequenceGpuProfilingRestore)
		{
			m_SequenceGpuProfilingRestore = view->IsEnabled();
			control->RequestEnabled(true);
		}
		else if (!timingWanted && m_SequenceGpuProfilingRestore)
		{
			control->RequestEnabled(*m_SequenceGpuProfilingRestore);
			m_SequenceGpuProfilingRestore.reset();
		}
	}

}
