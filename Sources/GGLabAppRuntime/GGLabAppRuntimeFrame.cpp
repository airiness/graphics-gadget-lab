#include "GGLabAppRuntime.h"
#include "Parameters/HostViewParameters.h"
#include "Parameters/SessionParameterControl.h"
#include "Parameters/ViewControlSnapshot.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchDefaults.h"
#include "GGLabRuntime/Graphics/ViewRenderSettingsPatch.h"

#include "AppRuntimeLog.h"
#include "ApplicationInput.h"
#include "ApplicationToolingIntegration.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/NonFiniteWatchCoordinator.h"
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
#include "GGLabRuntime/Graphics/RenderScene.h"
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
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RHI/RHISwapChain.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingControlBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingViewBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
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
			const AssetManager& m_AssetManager;
			const RenderServices& m_RenderServiceBindings;
			const Time& m_Time;
			const ShaderPreloadStatus& m_ShaderPreload;
			const CameraRig::CameraSlot& m_DisplayCameraSlot;
			std::span<const CameraReferenceView> m_ReferenceViews;
			const ResolvedTemporalFramePlan& m_TemporalFramePlan;
			const TemporalFrameTransaction& m_TemporalFrameTransaction;
			const TemporalAASettings& m_TemporalSettings;
			const GTAOSettings& m_GTAOSettings;
			ViewResolution m_ViewResolution{};
			uint64_t m_FrameSerial = 0;
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
				shaders.IsReady() ?
					FrameCaptureGateState::Ready :
					shaders.HasFailed() ?
						FrameCaptureGateState::Failed :
						FrameCaptureGateState::Pending,
				shaders.HasFailed() ?
					shaders.m_Error :
					shaders.IsReady() ?
						std::string{} :
						std::format("{} of {} shaders preloaded.", shaders.m_CompletedCount,
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
			// requested environment lighting is fully baked or cache-restored. The
			// request is read live: an environment committed this frame requests its
			// bake before the bake status observes it.
			const RenderEnvironmentAccess& environment = *inputs.m_RenderServiceBindings.m_Environment;
			const IBLBakeStatus& bake = environment.GetBakingStatus();
			const bool iblPending = environment.GetBakeRequestGeneration() != bake.m_ActiveGeneration;
			readiness.Add("ibl",
				iblPending ? FrameCaptureGateState::Pending : FrameCaptureGateState::Ready,
				iblPending ?
					std::format("Baking {} ({:.0f}%).", GetIBLBakeStageName(bake.m_Stage),
						bake.m_Progress * 100.0f) :
					std::string{}, "lighting", environment.GetBakeRequestGeneration());

			// A load reaches the upload queues only once its CPU work completes, so
			// asset uploads alone would report Ready while a decode is in flight.
			const uint32_t pendingLoads = inputs.m_AssetManager.GetInFlightLoadCount();
			readiness.Add("asset-loads",
				pendingLoads > 0 ? FrameCaptureGateState::Pending : FrameCaptureGateState::Ready,
				pendingLoads > 0 ?
					std::format("{} asset loads in flight.", pendingLoads) :
					std::string{});

			const AssetUploadStatistics uploads =
				inputs.m_RenderServiceBindings.m_AssetUpload->GetStatistics();
			const uint32_t pendingUploads = uploads.m_PendingCount +
				uploads.m_CpuPayloadQueue.m_PendingCount +
				uploads.m_ResourcePublicationQueue.m_PendingCount +
				uploads.m_UploadRecordingQueue.m_PendingCount +
				uploads.m_GpuFinalizeQueue.m_PendingCount;
			readiness.Add("asset-uploads",
				pendingUploads > 0 ? FrameCaptureGateState::Pending : FrameCaptureGateState::Ready,
				pendingUploads > 0 ?
					std::format("{} uploads pending.", pendingUploads) :
					std::string{});
			return readiness;
		}

		[[nodiscard]] std::vector<std::string> GetTemporalServiceNames(
			TemporalService services) noexcept
		{
			std::vector<std::string> names;
			for (const TemporalService service : TemporalServices)
			{
				if (Test(services, service))
				{
					names.emplace_back(GetTemporalServiceName(service));
				}
			}
			return names;
		}

		[[nodiscard]] FrameCaptureTemporalState BuildCaptureTemporalState(
			const CaptureFrameInputs& inputs) noexcept
		{
			const ResolvedTemporalFramePlan& plan = inputs.m_TemporalFramePlan;
			const TemporalConsumerPlan& temporalAA =
				plan.GetConsumer(TemporalConsumer::TemporalAA);
			std::vector<FrameCaptureTemporalConsumer> consumers;
			consumers.reserve(TemporalConsumerCount);
			for (uint32_t index = 0; index < TemporalConsumerCount; ++index)
			{
				const auto consumer = static_cast<TemporalConsumer>(index);
				const TemporalConsumerPlan& consumerPlan = plan.GetConsumer(consumer);
				consumers.push_back({
					.m_Name = std::string(GetTemporalConsumerName(consumer)),
					.m_Requested = consumerPlan.m_Requested,
					.m_Status = std::string(GetTemporalConsumerStatusName(consumerPlan.m_Status)),
					.m_DisableReason = std::string(
						GetTemporalConsumerDisableReasonName(consumerPlan.m_DisableReason)),
					.m_Services = GetTemporalServiceNames(consumerPlan.m_Services),
				});
			}
			const TemporalAASettings& settings = inputs.m_TemporalSettings;
			const TemporalFrameTransaction& transaction = inputs.m_TemporalFrameTransaction;
			const Vector2& jitter = transaction.GetJitterPixels();
			const ViewResolution& resolution = inputs.m_ViewResolution;
			return FrameCaptureTemporalState{
				.m_Requested = temporalAA.m_Requested,
				.m_Status = std::string(GetTemporalConsumerStatusName(temporalAA.m_Status)),
				.m_DisableReason = std::string(
					GetTemporalConsumerDisableReasonName(temporalAA.m_DisableReason)),
				.m_Consumers = std::move(consumers),
				.m_Services = GetTemporalServiceNames(plan.m_Services),
				.m_SessionIdentity = plan.m_SessionIdentity,
				.m_ResetIdentity = plan.m_ResetIdentity,
				.m_JitterIndex = transaction.GetJitterIndex(),
				.m_JitterSequenceLength = transaction.GetJitterSequenceLength(),
				.m_JitterPixels = { jitter.m_X, jitter.m_Y },
				.m_MaxHistoryFeedback = settings.m_MaxHistoryFeedback,
				.m_DepthAbsoluteThreshold = settings.m_DepthAbsoluteThreshold,
				.m_DepthRelativeThreshold = settings.m_DepthRelativeThreshold,
				.m_VelocityWeightScale = settings.m_VelocityWeightScale,
				.m_LuminanceWeightScale = settings.m_LuminanceWeightScale,
				.m_NeighborhoodClampExpansion = settings.m_NeighborhoodClampExpansion,
				.m_HistoryRelaxation = settings.m_HistoryRelaxation,
				.m_HistoryAccumulation = std::string(
					GetTemporalAAHistoryAccumulationName(settings.m_HistoryAccumulation)),
				.m_HistoryRectification = std::string(
					GetTemporalAAHistoryRectificationName(settings.m_HistoryRectification)),
				.m_VarianceClipGamma = settings.m_VarianceClipGamma,
				.m_HistoryFilter =
					std::string(GetTemporalAAHistoryFilterName(settings.m_HistoryFilter)),
				.m_CurrentFilter =
					std::string(GetTemporalAACurrentFilterName(settings.m_CurrentFilter)),
				.m_MotionSelection =
					std::string(GetTemporalAAMotionSelectionName(settings.m_MotionSelection)),
				.m_PostTemporalView =
					std::string(GetTemporalAAPostTemporalViewName(settings.m_PostTemporalView)),
				.m_ResolutionPreset =
					std::string(GetTemporalAAResolutionPresetName(settings.m_ResolutionPreset)),
				.m_TextureLodBiasOffset = settings.m_TextureLodBiasOffset,
				.m_GTAO = {
					.m_Enabled = inputs.m_GTAOSettings.m_Enabled,
					.m_Radius = inputs.m_GTAOSettings.m_Radius,
					.m_FalloffStart = inputs.m_GTAOSettings.m_FalloffStart,
					.m_FalloffEnd = inputs.m_GTAOSettings.m_FalloffEnd,
					.m_DirectionCount = inputs.m_GTAOSettings.m_DirectionCount,
					.m_StepCount = inputs.m_GTAOSettings.m_StepCount,
					.m_DenoiseRadius = inputs.m_GTAOSettings.m_DenoiseRadius,
					.m_TemporalAccumulation = inputs.m_GTAOSettings.m_TemporalAccumulation,
					.m_TemporalMaxSamples = inputs.m_GTAOSettings.m_TemporalMaxSamples,
				},
				.m_TextureLodBias = transaction.GetTextureLodBias(),
				.m_RenderExtent = { resolution.m_Render.m_Width, resolution.m_Render.m_Height },
				.m_DisplayExtent = { resolution.m_Display.m_Width, resolution.m_Display.m_Height },
				.m_RenderScale = resolution.m_Display.m_Width > 0 ?
					static_cast<float>(resolution.m_Render.m_Width) /
						static_cast<float>(resolution.m_Display.m_Width) :
					0.0f,
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
				.m_DemoId = std::string(demoIndex < inputs.m_DemoManager.GetDemoCount() ?
					inputs.m_DemoManager.GetDemoName(demoIndex) :
					inputs.m_Demo.GetName()),
				.m_LabId = inputs.m_Demo.GetCaptureContentId(),
				.m_AssetBundle = inputs.m_Demo.GetCaptureAssetBundle(),
				.m_Readiness = BuildCaptureReadiness(inputs),
				.m_SettleKey = {
					.m_Temporal = inputs.m_TemporalFrameTransaction.GetContinuity(),
					.m_DemoIndex = demoIndex,
				},
				.m_DisplayFormat = inputs.m_RenderServiceBindings.m_Presentation->GetSwapChain()->GetFormat(),
				.m_FrameSerial = inputs.m_FrameSerial,
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
		m_RenderHost->RevokeRenderServices();
		GGLAB_LOG_CRITICAL_ALWAYS("Fatal runtime failure: {}", failure);
		m_LifecycleState = AppRuntimeLifecycleState::Failed;
		if (m_ParameterControl)
		{
			m_ParameterControl->OnRuntimeFailed(failure);
			UpdateSequenceParameterEvents();
			m_ToolingViewControl.Update();
		}
		if (m_FrameSequence && m_FrameSequence->GetStatus() &&
			!m_FrameSequence->GetStatus()->m_Events.empty())
		{
			m_FrameSequence->OnParameterEventFailed(std::string(failure));
			UpdateSequenceParameterEvents();
		}
		if (m_DemoManager)
		{
			if (DemoBase* demo = m_DemoManager->GetActiveDemo())
			{
				demo->OnRuntimeFailed(failure);
			}
		}
		return AppRuntimeTickResult::Fatal;
	}

	AppRuntimeTickResult GGLabAppRuntime::Tick(AppRuntimeTickInfo tickInfo) noexcept
	{
		ServiceControlDeadlines();
		m_ToolingViewControl.Update();
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

		m_ToolingFrameSettings = tickInfo.m_ApplicationTooling ?
			tickInfo.m_ApplicationTooling->CaptureFrameSettings() :
			ApplicationToolingFrameSettings{};
		m_FrameCapture->Update();
		CollectFrameCaptureResults();
		SyncSequenceGpuProfiling();
		CollectSequenceGpuTiming();
		const AppRuntimeTickResult result = TickFrame(tickInfo);
		// Also route synchronous failures from planning, including early exits.
		CollectFrameCaptureResults();
		CollectSequenceGpuTiming();
		SyncSequenceGpuProfiling();
		m_ToolingViewControl.Update();
		return result;
	}

	AppRuntimeTickResult GGLabAppRuntime::TickFrame(AppRuntimeTickInfo tickInfo) noexcept
	{
		// A sequence capture without payload or write capacity defers the
		// whole frame, before time advances, so no extra frame is simulated or
		// rendered while the writer drains.
		if (m_FrameSequence->ShouldDeferFrame())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			return AppRuntimeTickResult::Continue;
		}

		GGLAB_CPU_PROFILE_FRAME(m_Time->GetFrameCount() + 1);

		if (m_FrameSequence->ShouldHoldTime() || m_FrameCapture->ShouldHoldTime())
		{
			// Every sample of a supersampled reference frame renders the same instant,
			// and an after-ready capture does not advance time before it settles.
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

		if (m_Input->IsKeyPressed(AppInputKey::T))
		{
			m_Input->SetPointerMode(m_Input->GetPointerMode() == AppPointerMode::Absolute ?
				AppPointerMode::Relative :
				AppPointerMode::Absolute);
		}

		if (m_Input->IsKeyPressed(AppInputKey::Escape))
		{
			m_RenderHost->RevokeRenderServices();
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
		// Binding is a content lifecycle stage, including when no RHI frame is ready.
		SynchronizeContentBinding();
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
		const ApplicationToolingInputCapture toolingInputCapture = applicationTooling ?
			applicationTooling->GetPreviousFrameInputCapture() :
			ApplicationToolingInputCapture{};
		m_Input->SetUICaptureState(
			toolingInputCapture.m_Keyboard, toolingInputCapture.m_Pointer);

		DemoBase* demo = m_DemoManager->GetActiveDemo();
		GGLAB_ASSERT_NOT_NULL(demo);
		demo->Update();
		// Lab transitions may replace content during Update without replacing the Demo.
		SynchronizeContentBinding();
		m_AssetManager->Tick();
		m_EnvironmentAssetController->Tick();
		if (m_FrameCapture->GetUnfinishedRequestCount() || m_FrameSequence->IsActive())
		{
			m_ParameterControl->Enable();
		}

		World& world = demo->GetWorld();
		RenderFrame rendererFrame = m_RenderHost->BeginFrame();
		if (!rendererFrame.IsReady())
		{
			return rendererFrame.IsUnavailable() ?
				AppRuntimeTickResult::Continue :
				FailRuntime("The render host failed to begin a frame.");
		}
		const auto sequencePose = m_FrameSequence->PrepareFrame(
			m_FrameCapture->GetLastFrameState(), demo->GetCameraRig().GetCameraPaths(),
			m_ParameterControl->GetContentIdentity(),
			[this](std::span<FrameSequenceParameterEvent> events, std::string& error)
			{
				for (auto& event : events)
				{
					if (!m_ParameterControl->ValidateSequenceAssignments(
						event.m_Values, event.m_ExpectEffects, error))
					{
						return false;
					}
				}
				return true;
			});
		if (const auto* event = m_FrameSequence->GetDueParameterEvent())
		{
			std::string error;
			const auto owner = ParameterBatchOwner::Sequence(m_FrameSequence->GetStatus()->m_SequenceId);
			const auto request = m_ParameterControl->MakeAssignmentRequest(event->m_Values);
			const auto batch = m_ParameterControl->RequestParameters(request, error, owner);
			if (!batch)
			{
				m_FrameSequence->OnParameterEventFailed(std::move(error));
				return AppRuntimeTickResult::Continue;
			}
			UpdateSequenceParameterEvents();
		}
		if (m_ParameterControl->IsEnabled())
		{
			m_ParameterControl->OnFrameReady(rendererFrame.GetSerial());
		}
		demo->OnFrameReady(rendererFrame.GetSerial());
		ApplicationToolingFrame toolingFrame(applicationTooling);
		RenderServices services = m_RenderServiceBindings;
		services.m_TextureAssets = m_AssetManager.get();
		services.m_OverlayExtension = toolingFrame.GetOverlayExtension();
		// The RAII frame handle may retire RenderGraph resources from its abort
		// path. Keep the graph alive until after the frame has ended.
		RenderGraph renderGraph(m_RenderHost->CreateRenderGraphCreateInfo());
		const uint32_t frameSlotIndex = rendererFrame.GetFrameSlotIndex();
		const uint32_t backBufferIndex = rendererFrame.GetBackBufferIndex();

		ShadowVisualizationSettings shadowVisualizationSettings = m_ToolingFrameSettings.m_Shadow;
		const ViewRenderProfile& authoringViewRenderProfile = demo->GetViewRenderProfile();
		CameraRig& cameraRig = demo->GetCameraRig();
		// A capture's reference view is restored before the frame is planned, so
		// this frame already renders it.
		if (const std::optional<FrameCaptureViewChange> viewChange =
			m_FrameCapture->GetPendingViewChange())
		{
			m_FrameCapture->OnReferenceViewApplied(viewChange->m_JobId,
				cameraRig.RestoreReferenceView(viewChange->m_ReferenceViewId));
		}
		// A running sequence poses the main camera after content updates and capture
		// views, so the path alone determines this frame's camera.
		std::optional<TemporalReferenceSample> referenceSample;
		std::optional<FrameSequenceTemporalAAOverrides> sequenceTemporalAAOverrides;
		std::optional<FrameSequenceGTAOOverrides> sequenceGTAOOverrides;
		if (sequencePose)
		{
			const std::optional<CameraPathPose> pose = cameraRig.ApplyCameraPathFrame(
				sequencePose->m_CameraPathId, sequencePose->m_Frame);
			m_FrameSequence->OnPoseApplied(pose);
			if (const CameraPath* path = pose ?
				cameraRig.FindCameraPath(sequencePose->m_CameraPathId) :
				nullptr)
			{
				demo->OnCameraPathFrameApplied(*path, sequencePose->m_Frame);
			}
			referenceSample = sequencePose->m_ReferenceSample;
			sequenceTemporalAAOverrides = sequencePose->m_TemporalAAOverrides;
			sequenceGTAOOverrides = sequencePose->m_GTAOOverrides;
		}
		const CameraRig::EffectiveDisplayView effectiveDisplayView =
			cameraRig.ResolveEffectiveDisplayView();
		GGLAB_ASSERT_MSG(effectiveDisplayView.IsValid(),
			"CameraRig must resolve one effective display view before "
			"frame planning.");
		const CameraRig::CameraSlot* displayCameraSlot = effectiveDisplayView.m_CameraSlot;
		// An after-ready capture settles over temporal history of ready frames
		// only. A cut drops history that still holds frames of loading content.
		if (m_FrameCapture->ShouldRestartTemporalHistory())
		{
			displayCameraSlot->m_Camera->RequestTemporalReset();
		}
		const auto inheritedView = ResolveApplicationViewSettings(
			authoringViewRenderProfile, *displayCameraSlot->m_Camera, m_ToolingFrameSettings);
		const auto& effectiveViewRenderProfile = inheritedView.m_Profile;
		ResolvedViewRenderSettings displayViewSettings = inheritedView.m_View;
		m_ParameterControl->ApplyViewSettings(displayViewSettings,
			effectiveViewRenderProfile.m_EnableScenePreExposure);
		// Records which fields the frame-local layer below writes, for value attribution.
		ViewRenderSettingsPatch framePatch;
		if (referenceSample)
		{
			// The reference owns jitter and accumulation; Temporal AA and temporal GTAO
			// stay inactive while it averages its own sample sequence.
			displayViewSettings.m_TemporalAA.m_Enabled = false;
			displayViewSettings.m_Lighting.m_GTAO.m_TemporalAccumulation = false;
			framePatch.m_TemporalAA.m_Enabled = false;
			framePatch.m_Lighting.m_GTAO.m_TemporalAccumulation = false;
		}
		else
		{
			if (sequenceTemporalAAOverrides)
			{
				displayViewSettings.m_TemporalAA = ApplyFrameSequenceTemporalAAOverrides(
					*sequenceTemporalAAOverrides, displayViewSettings.m_TemporalAA);
				framePatch.m_TemporalAA = *sequenceTemporalAAOverrides;
			}
			if (sequenceGTAOOverrides)
			{
				displayViewSettings.m_Lighting.m_GTAO = ApplyFrameSequenceGTAOOverrides(
					*sequenceGTAOOverrides, displayViewSettings.m_Lighting.m_GTAO);
				framePatch.m_Lighting.m_GTAO = ToGTAOSettingsPatch(*sequenceGTAOOverrides);
			}
		}
		const uint64_t temporalSessionIdentity = m_ActiveContentIdentity;
		RenderPipelineBase& renderPipeline = demo->GetRenderPipeline();
		const ResolvedTemporalFramePlan temporalFramePlan =
			renderPipeline.ResolveTemporalFramePlan({
				.m_TemporalAA = {
					.m_Settings = displayViewSettings.m_TemporalAA,
					.m_Capabilities = m_RenderHost->GetTemporalAACapabilityStatus(),
				},
				.m_DisplayViewId = effectiveDisplayView.m_ViewId,
				.m_ResetIdentity = displayCameraSlot->m_Camera->GetTemporalResetSerial(),
				.m_SessionIdentity = temporalSessionIdentity,
				.m_DisplayViewEligible = IsTemporalAADisplayViewEligible(
					effectiveDisplayView.m_ViewId, m_WindowWidth, m_WindowHeight),
				.m_NativeResolutionRequired = HasMaterialDiagnosticDemand(world, *m_AssetManager),
				.m_ReferenceRequested = referenceSample.has_value(),
				.m_AmbientOcclusionRequested =
					displayViewSettings.m_Lighting.m_GTAO.m_Enabled &&
					displayViewSettings.m_Lighting.m_GTAO.m_TemporalAccumulation,
				.m_AmbientOcclusionAvailable =
					m_RenderHost->GetGTAOCapabilityStatus().IsTemporalAvailable(),
			});
		// The window client extent is the display extent; the plan's effective preset
		// derives the render extent from it.
		const ViewResolution viewResolution = ResolveTemporalAAViewResolution(
			{ m_WindowWidth, m_WindowHeight }, temporalFramePlan.m_TemporalAA.m_ResolutionPreset);
		TemporalFrameTransaction& temporalFrameTransaction = m_RenderHost->BeginTemporalFrame(
			rendererFrame, temporalFramePlan, viewResolution,
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
			.m_ViewResolution = viewResolution,
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
			.m_AssetManager = *m_AssetManager,
			.m_RenderServiceBindings = m_RenderServiceBindings,
			.m_Time = *m_Time,
			.m_ShaderPreload = shaderPreload,
			.m_DisplayCameraSlot = *displayCameraSlot,
			.m_ReferenceViews = cameraRig.GetReferenceViews(),
			.m_TemporalFramePlan = temporalFramePlan,
			.m_TemporalFrameTransaction = temporalFrameTransaction,
			.m_TemporalSettings = displayViewSettings.m_TemporalAA,
			.m_GTAOSettings = displayViewSettings.m_Lighting.m_GTAO,
			.m_ViewResolution = viewResolution,
			.m_FrameSerial = rendererFrame.GetSerial(),
			.m_Backend = m_Config.m_RhiBackend,
			.m_DevelopmentTools = applicationTooling != nullptr,
		});
		captureFrameState.m_ParameterRevision = m_ParameterControl->GetParameterRevision();
		captureFrameState.m_HostViewControl = GetHostViewControlStamp();
		captureFrameState.m_HostViewProjection = DescribeHostViewProjection(displayViewSettings,
			inheritedView.m_ToolingBlocks, m_ParameterControl->GetHostViewOverrides(), {
				.m_Sequence = referenceSample ? nullptr : &framePatch,
				.m_ForcedReference = referenceSample ? &framePatch : nullptr,
			});

		RenderFrameBuildResult frame;
		{
			GGLAB_CPU_PROFILE_SCOPE("RenderHostFrameBuilder");
			frame = m_RenderHost->BuildFrame(frameBuildRequest);
		}
		m_ParameterControl->OnFrameBuilt(frame.m_FrameSerial, frame.m_RenderScene.m_WorldLightingGeneration,
			frame.m_ViewRenderSettings[utils::ToIndex(frame.m_DisplayViewId)],
			&frame.m_RenderViews[utils::ToIndex(frame.m_DisplayViewId)]);
		auto parameterFrame = m_ParameterControl->IsEnabled() ?
			std::make_shared<ParameterFrameState>(m_ParameterControl->CaptureFrameState(
				frame.m_FrameSerial, m_RenderServiceBindings.m_Environment->GetBakeRequestGeneration(),
				frame.m_RenderScene.m_WorldLightingGeneration)) : nullptr;
		UpdateSequenceParameterEvents();
		demo->OnFrameBuilt(frame.m_FrameSerial, frame.m_RenderScene.m_WorldLightingGeneration);
		RenderFrameContext validationContext = frame.MakeRenderFrameContext();
		const auto& capturedView = frame.m_ViewRenderSettings[utils::ToIndex(frame.m_DisplayViewId)];

		const RenderFrameValidationResult validation =
			renderPipeline.ValidateRenderFrame(validationContext, services);
		if (!validation.IsReady())
		{
			// Preserve the aborted frame record even though successful capture selection
			// now waits for the validated observation identity.
			m_FrameCapture->BeginFrame(std::move(captureFrameState));
			// Nothing has been recorded. The temporal transaction is aborted and the
			// RenderFrame handle retires the begun frame on return.
			m_RenderHost->InvalidateTemporalFrameAfterLateContractFailure(rendererFrame);
			m_FrameCapture->OnFrameAborted();
			if (validation.m_Status == RenderFrameValidationStatus::Skipped)
			{
				toolingFrame.Complete();
				return AppRuntimeTickResult::Continue;
			}
			toolingFrame.Abort();
			return FailRuntime(std::format(
				"{}: pipeline='{}', frame={}, backend={}, reason='{}'{}{}",
				validation.m_Status == RenderFrameValidationStatus::DeviceLost ?
					"Rendering device lost" : "Rendering contract failure",
				renderPipeline.GetName(), frame.m_FrameSerial, GetBackendName(m_Config.m_RhiBackend),
				validation.m_Reason, validation.m_Detail.empty() ? "" : ", detail=",
				validation.m_Detail));
		}
		const auto reference = services.m_Presentation->GetGIReferenceObservationDiagnostics();
		if (reference.m_Active)
		{
			captureFrameState.m_Readiness.Add("gi-reference",
				reference.m_Status == GIReferenceObservationStatus::Completed ? FrameCaptureGateState::Ready :
					reference.m_Status == GIReferenceObservationStatus::Failed ? FrameCaptureGateState::Failed :
						FrameCaptureGateState::Pending,
				std::format("Observation {}: D={}, {}/{} completed samples, {} submitted, version {}; sweep {} samples, tiles {}/{} submitted.",
					reference.m_Generation, reference.m_MaximumDepth, reference.m_CompletedSamples, reference.m_TargetSamples,
					reference.m_SubmittedSamples, reference.m_CommittedVersion, reference.m_SweepSamples,
					reference.m_SubmittedTiles, reference.m_TileCount),
				"gi-reference", reference.m_Generation);
		}
		// Select captures only after validation has identified this frame's observation.
		if (!m_FrameSequence->BeginFrame(captureFrameState, [this](const FrameCaptureGate& gate)
			{
				return m_ParameterControl->CoversReadinessGate(
					ParameterBatchOwner::Sequence(m_FrameSequence->GetStatus()->m_SequenceId), gate);
			}))
		{
			UpdateSequenceParameterEvents();
			toolingFrame.Complete();
			return AppRuntimeTickResult::Continue;
		}
		// Freeze the previous outcome before BeginFrame replaces it with this plan.
		// Observation must not enable parameter tracking or reset capture settling.
		const std::optional<ViewControlSnapshot> viewControl = toolingFrame.IsOpen() ?
			std::optional(BuildViewControlSnapshot(inheritedView, m_ParameterControl->GetHostViewOverrides(),
				captureFrameState, m_FrameCapture->GetLastFrameState())) :
			std::nullopt;
		m_FrameCapture->BeginFrame(std::move(captureFrameState));
		m_FrameCapture->SetFrameSceneInputs(frame.m_FrameSerial, {
			.m_Exposure = capturedView.m_Exposure,
			.m_PostProcess = capturedView.m_PostProcess,
			.m_Shadows = frame.m_DirectionalShadowSettings,
			.m_Environment = m_RenderServiceBindings.m_Environment->GetEnvironmentLightingSettings(),
			.m_ActiveIBLGeneration = frame.m_RenderScene.m_WorldLightingGeneration,
			.m_Atmosphere = frame.m_RenderScene.m_Atmosphere,
			.m_Sun = frame.m_RenderScene.m_WorldSun,
			.m_LocalLightCount = frame.m_RenderScene.m_LocalLightCount,
			.m_AerialPerspective = capturedView.m_Lighting.m_EnableAerialPerspective,
		});
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
			m_FrameCapture->OnFrameAborted();
			return FailRuntime("RenderGraph compilation failed.");
		}
		const auto defaults = m_NonFiniteWatch->UsesDefaultTargets() ?
			ResolveNonFiniteWatchDefaults(renderGraph) :
			NonFiniteWatchDefaults{};
		m_NonFiniteWatch->PrepareFrame(frame.m_FrameSerial, m_FrameCapture->HasFrameResourceExports(), defaults);
		const auto& exportView = frame.m_RenderViews[utils::ToIndex(frame.m_DisplayViewId)];
		if (!m_FrameCapture->PrepareResourceExports(frame.m_FrameSerial, renderGraph,
			{ .m_PreExposure = exportView.m_ScenePreExposure,
				.m_ReversedDepth = exportView.m_DepthConvention == DepthConvention::Reversed }))
		{
			// The RenderFrame aborts before submission. Sequence pose/sample and
			// capture metadata are retried together when the complete group fits.
			m_FrameCapture->OnFrameAborted();
			toolingFrame.Complete();
			return AppRuntimeTickResult::Continue;
		}

		if (toolingFrame.IsOpen())
		{
			GGLAB_CPU_PROFILE_SCOPE("ApplicationTooling");
			const LabRuntime* labRuntime = tickInfo.m_LabRuntimeLocator ?
				tickInfo.m_LabRuntimeLocator->GetLabRuntimeIfCreated() :
				nullptr;
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
				.m_GTAOOverrideActive = m_ToolingFrameSettings.m_GTAO.has_value(),
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
				.m_ViewControl = viewControl ? &*viewControl : nullptr,
				.m_ViewControlActions = &m_ToolingViewControl,
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
			m_FrameCapture->OnFrameAborted();
			toolingFrame.Abort();
			return FailRuntime("The render host failed to complete frame submission.");
		}
		m_NonFiniteWatch->OnFrameSubmitted(frame.m_FrameSerial);
		m_FrameCapture->OnFrameSubmitted();
		const FrameSequenceObservation sequenceObservation = m_FrameSequence->WantsGpuTiming() ?
			FrameSequenceObservation{ m_FrameCapture->HasFrameGraphExports(frame.m_FrameSerial),
				renderGraph.GetObservationActivity() } :
			FrameSequenceObservation{};

		m_ParameterControl->OnFrameSubmitted({
			.m_RenderSceneStatus = frame.m_RenderSceneStatus,
			.m_SubmittedFence = frameEndResult.GetSubmittedFence(),
			.m_RenderFrameSerial = frame.m_FrameSerial,
		});
		if (parameterFrame)
		{
			m_ParameterControl->CompleteFrameState(*parameterFrame);
			m_FrameCapture->SetFrameParameters(frame.m_FrameSerial, parameterFrame);
		}
		UpdateSequenceParameterEvents();
		m_FrameSequence->OnFrameSubmitted(frame.m_FrameSerial, sequenceObservation, parameterFrame);
		UpdateSequenceParameterEvents();
		m_DemoManager->OnFrameSubmitted({
			.m_RenderSceneStatus = frame.m_RenderSceneStatus,
			.m_SubmittedFence = frameEndResult.GetSubmittedFence(),
			.m_FrameIndex = m_Time->GetFrameCount(),
			.m_BackBufferIndex = backBufferIndex,
			.m_RenderFrameSerial = frame.m_FrameSerial,
			.m_TemporalAASettings = frame.m_ViewRenderSettings[
				utils::ToIndex(frame.m_DisplayViewId)].m_TemporalAA,
			.m_TemporalFramePlan = frame.m_TemporalFramePlan,
			.m_ViewResolution = viewResolution,
		});

		// Pipelines without an overlay pass still complete the optional tooling frame.
		toolingFrame.Complete();
		return AppRuntimeTickResult::Continue;
	}

	void GGLabAppRuntime::CollectSequenceGpuTiming() noexcept
	{
		if (m_FrameSequence->WantsGpuTiming())
		{
			const GpuProfilingViewBase* view = m_RenderHost->GetGpuProfilingView();
			const bool enabled = view && view->IsEnabled();
			m_FrameSequence->UpdateGpuTiming(
				enabled ? view->GetLatestFrame() : GpuProfileFrameSnapshot{}, enabled);
		}
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
