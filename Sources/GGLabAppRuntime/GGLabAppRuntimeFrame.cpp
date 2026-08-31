#include "GGLabAppRuntime.h"

#include "AppRuntimeLog.h"
#include "ApplicationInput.h"
#include "ApplicationToolingIntegration.h"
#include "Core/Profiling/CpuProfiler.h"
#include "Core/Time.h"
#include "Demo/DemoBase.h"
#include "Demo/DemoManager.h"
#include "Demo/DemoTypes.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/CameraRig.h"
#include "Graphics/DebugDraw/DebugDrawSystem.h"
#include "Graphics/EnvironmentAssetController.h"
#include "Graphics/RenderFrameBuilder.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/Shader/ShaderManager.h"
#include "LoadingProgress.h"

#include <optional>
#include <span>

namespace gglab
{
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
		Camera& camera = demo->GetCamera();
		Renderer::Frame rendererFrame = m_Renderer->BeginFrame();
		if (!rendererFrame.IsReady())
		{
			return rendererFrame.IsUnavailable()
				? AppRuntimeTickResult::Continue
				: AppRuntimeTickResult::Exit;
		}
		ApplicationToolingFrame toolingFrame(applicationTooling);
		const RenderServices services{
			.m_Renderer = m_Renderer.get(),
			.m_AssetManager = m_AssetManager.get(),
			.m_ShaderManager = m_ShaderManager.get(),
			.m_OverlayExtension = toolingFrame.GetOverlayExtension(),
		};
		// Renderer::Frame may retire RenderGraph resources from its RAII abort path.
		// Keep the graph alive until after the frame has ended.
		RenderGraph renderGraph(m_Renderer->CreateRenderGraphCreateInfo());
		const uint32_t frameSlotIndex = rendererFrame.GetFrameSlotIndex();
		const uint32_t backBufferIndex = rendererFrame.GetBackBufferIndex();

		ShadowVisualizationSettings shadowVisualizationSettings =
			DefaultShadowVisualizationSettings();
		const ViewRenderProfile& authoringViewRenderProfile = demo->GetViewRenderProfile();
		ViewRenderProfile effectiveViewRenderProfile = authoringViewRenderProfile;
		if (applicationTooling)
		{
			applicationTooling->ResolveFrameSettings(authoringViewRenderProfile,
				shadowVisualizationSettings, effectiveViewRenderProfile);
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
				.m_Capabilities = m_Renderer->GetTemporalAACapabilityStatus(),
				.m_DisplayViewId = effectiveDisplayView.m_ViewId,
				.m_ResetIdentity = displayCameraSlot->m_Camera->GetTemporalResetSerial(),
				.m_SessionIdentity = temporalSessionIdentity,
				.m_DisplayViewEligible = IsTemporalAADisplayViewEligible(
					effectiveDisplayView.m_ViewId, m_WindowWidth, m_WindowHeight),
			});
		TemporalFrameTransaction& temporalFrameTransaction = m_Renderer->BeginTemporalFrame(
			rendererFrame, temporalFramePlan, m_WindowWidth, m_WindowHeight);
		const RenderFrameBuilder::BuildInfo frameBuildInfo{
			.m_World = world,
			.m_CameraRig = demo->GetCameraRig(),
			.m_Renderer = *m_Renderer,
			.m_AssetManager = *m_AssetManager,
			.m_ShadowVisualizationSettings = shadowVisualizationSettings,
			.m_ViewRenderProfile = effectiveViewRenderProfile,
			.m_TemporalFramePlan = temporalFramePlan,
			.m_TemporalFrameTransaction = &temporalFrameTransaction,
			.m_DisplayViewId = effectiveDisplayView.m_ViewId,
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
		RenderFrameContext validationContext = frame.MakeRenderFrameContext();
		m_Renderer->AdoptFrameBuildResources(rendererFrame, validationContext);
		if (!renderPipeline.ValidateRenderFrame(validationContext, services))
		{
			m_Renderer->InvalidateTemporalFrameAfterLateContractFailure(rendererFrame);
			toolingFrame.Complete();
			return AppRuntimeTickResult::Continue;
		}
		demo->GetCameraRig().SubmitDebugDraw(m_DebugDrawSystem->GetContext());
		frame.m_DebugDrawFrame = m_DebugDrawSystem->SealFrame(frameSlotIndex,
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
			std::optional<LoadingProgress> loadingProgress;
			if (!shaderPreload.IsReady())
			{
				loadingProgress = GetStartupLoadingProgress();
			}
			else
			{
				loadingProgress = m_DemoManager->GetLoadingProgress();
			}

			const ApplicationToolingFrameContext toolingContext{
				.m_Camera = &camera,
				.m_CameraController = &demo->GetCameraController(),
				.m_CameraRig = &demo->GetCameraRig(),
				.m_Renderer = m_Renderer.get(),
				.m_World = &world,
				.m_RenderViews = std::span<RenderView>(frame.m_RenderViews),
				.m_RenderQueues = std::span<const RenderQueue>(frame.m_RenderQueues),
				.m_MainRenderView =
					&frame.m_RenderViews[utils::ToIndex(RenderViewID::Main)],
				.m_AssetManager = m_AssetManager.get(),
				.m_EnvironmentAssetController = m_EnvironmentAssetController.get(),
				.m_RenderGraph = &renderGraph,
				.m_DebugDrawSystem = m_DebugDrawSystem.get(),
				.m_DebugDrawFrame = &frame.m_DebugDrawFrame,
				.m_DirectionalShadowSettings =
					frame.m_WorldData.m_MainDirectionalLight.m_ShadowSettings,
				.m_AuthoringViewRenderProfile = &authoringViewRenderProfile,
				.m_EffectiveViewRenderProfile = &effectiveViewRenderProfile,
				.m_TemporalFramePlan = &frame.m_TemporalFramePlan,
				.m_LoadingProgress = loadingProgress ? &*loadingProgress : nullptr,
			};
			toolingFrame.Draw(toolingContext);
		}

		{
			GGLAB_CPU_PROFILE_SCOPE("RenderGraph Execute");
			m_Renderer->Render(rendererFrame, renderGraph, renderContext);
		}
		RHIFrameEndResult frameEndResult = RHIFrameEndResult::Fatal();
		{
			GGLAB_CPU_PROFILE_SCOPE("Renderer EndFrame");
			frameEndResult = m_Renderer->EndFrame(rendererFrame);
		}
		if (!frameEndResult.IsCompleted())
		{
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
