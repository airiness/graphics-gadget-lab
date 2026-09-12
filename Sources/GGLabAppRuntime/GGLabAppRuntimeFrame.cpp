#include "GGLabAppRuntime.h"

#include "AppRuntimeLog.h"
#include "ApplicationInput.h"
#include "ApplicationToolingIntegration.h"
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
#include "GGLabRuntime/Graphics/CameraRig.h"
#include "GGLabRuntime/Graphics/DebugDraw/DebugDrawService.h"
#include "GGLabRuntime/Graphics/EnvironmentAssetController.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "Lab/LabInterfaces.h"
#include "Lab/LabRuntime.h"
#include "LoadingProgress.h"

#include <optional>
#include <span>

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
	}

	AppRuntimeTickResult GGLabAppRuntime::Tick(AppRuntimeTickInfo tickInfo) noexcept
	{
		switch (m_LifecycleState)
		{
		case AppRuntimeLifecycleState::Suspended:
			return AppRuntimeTickResult::Suspended;
		case AppRuntimeLifecycleState::Running:
			break;
		default:
			return AppRuntimeTickResult::Exit;
		}

		// The first initialization stage remains independently testable. Production
		// frame work starts only after the host has composed the runtime services.
		if (!m_ServicesInitialized)
		{
			return AppRuntimeTickResult::Continue;
		}

		GGLAB_CPU_PROFILE_FRAME(m_Time->GetFrameCount() + 1);

		m_Time->Update();
		m_TaskSystem->PumpCompletions({
			.m_MaxCallbacks = 64,
			.m_MaxMilliseconds = 1.0,
			});
		m_AssetManager->DrainLoadCompletions();

		if (m_Input->IsKeyPressed(AppInputKey::T))
		{
			m_Input->SetPointerMode(m_Input->GetPointerMode() == AppPointerMode::Absolute
				? AppPointerMode::Relative
				: AppPointerMode::Absolute);
		}

		if (m_Input->IsKeyPressed(AppInputKey::Escape))
		{
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
			GGLAB_LOG_ERROR("Host pre-content update failed.");
			return AppRuntimeTickResult::Exit;
		}
		if (shaderPreload.IsReady())
		{
			if (!m_DemoManager->CompleteTransitionTick())
			{
				GGLAB_LOG_ERROR("No active demo is available for rendering.");
				return AppRuntimeTickResult::Exit;
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
				: AppRuntimeTickResult::Exit;
		}
		ApplicationToolingFrame toolingFrame(applicationTooling);
		RenderServices services = m_RenderServices;
		services.m_AssetManager = m_AssetManager.get();
		services.m_ShaderManager = m_ShaderManager.get();
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
		const CameraRig::EffectiveDisplayView effectiveDisplayView =
			cameraRig.ResolveEffectiveDisplayView();
		GGLAB_ASSERT_MSG(effectiveDisplayView.IsValid(),
			"CameraRig must resolve one effective display view before "
			"frame planning.");
		const CameraRig::CameraSlot* displayCameraSlot = effectiveDisplayView.m_CameraSlot;
		const ResolvedViewRenderSettings displayViewSettings =
			ResolveViewRenderSettings(
				effectiveViewRenderProfile, *displayCameraSlot->m_Camera);
		const uint64_t temporalSessionIdentity =
			(static_cast<uint64_t>(m_DemoManager->GetTemporalSessionSerial()) << 32) |
			static_cast<uint64_t>(demo->GetTemporalSessionSerial());
		RenderPipelineBase& renderPipeline = demo->GetRenderPipeline();
		renderPipeline.PrepareTemporalFramePlanning(services);
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
			rendererFrame, temporalFramePlan, m_WindowWidth, m_WindowHeight);
		const RenderFrameBuildRequest frameBuildRequest{
			.m_World = world,
			.m_CameraRig = demo->GetCameraRig(),
			.m_ViewRenderProfile = effectiveViewRenderProfile,
			.m_ShadowVisualizationSettings = shadowVisualizationSettings,
			.m_TemporalFramePlan = temporalFramePlan,
			.m_TemporalFrameTransaction = temporalFrameTransaction,
			.m_DisplayViewId = effectiveDisplayView.m_ViewId,
			.m_WindowWidth = m_WindowWidth,
			.m_WindowHeight = m_WindowHeight,
			.m_FrameSlotIndex = frameSlotIndex,
			.m_BackBufferIndex = backBufferIndex,
			.m_FrameSerial = rendererFrame.GetSerial(),
		};
		RenderFrameBuildResult frame;
		{
			GGLAB_CPU_PROFILE_SCOPE("RenderHostFrameBuilder");
			frame = m_RenderHost->BuildFrame(frameBuildRequest);
		}
		RenderFrameContext validationContext = frame.MakeRenderFrameContext();
		if (!renderPipeline.ValidateRenderFrame(validationContext, services))
		{
			m_RenderHost->InvalidateTemporalFrameAfterLateContractFailure(rendererFrame);
			toolingFrame.Complete();
			return AppRuntimeTickResult::Continue;
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
			return AppRuntimeTickResult::Exit;
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
				.m_MainRenderView =
					&frame.m_RenderViews[utils::ToIndex(RenderViewID::Main)],
				.m_AuthoringViewRenderProfile = &authoringViewRenderProfile,
				.m_EffectiveViewRenderProfile = &effectiveViewRenderProfile,
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
			return AppRuntimeTickResult::Exit;
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

}
