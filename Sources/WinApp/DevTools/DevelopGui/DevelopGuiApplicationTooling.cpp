#include "Application/Tooling/ApplicationToolingComposition.h"
#include "Application/Demo/DemoLabRuntimeLocator.h"
#include "Demo/DemoManager.h"
#include "Lab/LabRuntime.h"
#include "ApplicationToolingIntegration.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiSystem.h"
#include "DevTools/DevelopGui/LoadingOverlay.h"
#include "DevTools/DevelopGui/Panels/DemoPanel.h"
#include "DevTools/DevelopGui/Panels/LabPanel.h"
#include "Diagnostics/Builders/LabSnapshotProvider.h"
#include "Diagnostics/DiagnosticsRuntime.h"

#include <memory>

namespace gglab
{
	namespace
	{
		class DevelopGuiApplicationTooling final : public ApplicationToolingIntegrationBase
		{
		public:
			DevelopGuiApplicationTooling() noexcept = default;
			DevelopGuiApplicationTooling(const DevelopGuiApplicationTooling&) = delete;
			DevelopGuiApplicationTooling& operator=(const DevelopGuiApplicationTooling&) = delete;
			~DevelopGuiApplicationTooling() override
			{
				m_System.Finalize();
			}

			void PrepareForShutdown() noexcept override
			{
				m_System.Finalize();
			}

			[[nodiscard]] bool Initialize(
				const ApplicationToolingCompositionCreateInfo& createInfo) noexcept
			{
				if (!createInfo.m_Window || !createInfo.m_RHIContext ||
					!createInfo.m_DemoManager ||
					!m_System.Initialize({
						.m_Window = createInfo.m_Window,
						.m_RHIContext = createInfo.m_RHIContext,
						.m_SettingsPath = createInfo.m_SettingsRoot / "imgui.ini",
						}))
				{
					return false;
				}

				auto& runtime = m_System.GetDevToolsRuntime();
				runtime.SetTaskSystem(createInfo.m_TaskSystem);
				runtime.GetRegistry().RegisterPanel(
					std::make_unique<DemoPanel>(createInfo.m_DemoManager));
				if (createInfo.m_LabRuntimeLocator)
				{
					runtime.GetDiagnostics().RegisterProvider(
						std::make_unique<LabSnapshotProvider>(
							[runtimeLocator = createInfo.m_LabRuntimeLocator]() noexcept
								-> const LabSnapshotSourceBase*
							{ return runtimeLocator->GetLabRuntimeIfCreated(); }),
						SnapshotUpdatePolicy::EveryFrame);
					runtime.GetRegistry().RegisterPanel(
						std::make_unique<LabPanel>(createInfo.m_LabRuntimeLocator));
				}
				return true;
			}

			ApplicationToolingInputCapture GetPreviousFrameInputCapture()
				const noexcept override
			{
				return {
					.m_Keyboard = m_System.WantsKeyboardCapture(),
					.m_Pointer = m_System.WantsMouseCapture(),
				};
			}

			void ResolveFrameSettings(const ViewRenderProfile& authoringProfile,
				ShadowVisualizationSettings& outShadowVisualizationSettings,
				ViewRenderProfile& outEffectiveProfile) const noexcept override
			{
				const auto& runtime = m_System.GetDevToolsRuntime();
				outShadowVisualizationSettings =
					runtime.GetRenderVisualizationSettings().m_Shadow;
				outEffectiveProfile = runtime.ResolveViewRenderProfile(authoringProfile);
			}

			bool BeginFrame() noexcept override { return m_System.BeginFrame(); }

			void Draw(const ApplicationToolingFrameContext& context) noexcept override
			{
				DevelopGuiContext guiContext{};
				guiContext.m_Camera = context.m_Camera;
				guiContext.m_CameraController = context.m_CameraController;
				guiContext.m_CameraRig = context.m_CameraRig;
				guiContext.m_Renderer = context.m_Renderer;
				guiContext.m_World = context.m_World;
				guiContext.m_RenderViews = context.m_RenderViews;
				guiContext.m_RenderQueues = context.m_RenderQueues;
				guiContext.m_MainRenderView = context.m_MainRenderView;
				guiContext.m_AssetManager = context.m_AssetManager;
				guiContext.m_EnvironmentAssetController =
					context.m_EnvironmentAssetController;
				guiContext.m_RenderGraph = context.m_RenderGraph;
				guiContext.m_DebugDrawSystem = context.m_DebugDrawSystem;
				guiContext.m_DebugDrawFrame =
					context.m_DebugDrawFrame ? *context.m_DebugDrawFrame : DebugDrawFrameView{};
				guiContext.m_DirectionalShadowSettings = context.m_DirectionalShadowSettings;
				guiContext.m_AuthoringViewRenderProfile =
					context.m_AuthoringViewRenderProfile;
				guiContext.m_EffectiveViewRenderProfile = context.m_EffectiveViewRenderProfile;
				guiContext.m_TemporalFramePlan = context.m_TemporalFramePlan;

				m_System.Draw(guiContext);
				if (context.m_LoadingProgress)
				{
					DrawLoadingOverlay(*context.m_LoadingProgress);
				}
			}

			void EndFrame(ApplicationToolingFrameEndReason) noexcept override
			{
				m_System.EndFrame();
			}

			RenderPipelineOverlayExtensionBase* GetOverlayExtension() noexcept override
			{
				return &m_System;
			}

		private:
			DevelopGuiSystem m_System;
		};
	}

	std::unique_ptr<ApplicationToolingIntegrationBase> CreateApplicationToolingIntegration(
		const ApplicationToolingCompositionCreateInfo& createInfo) noexcept
	{
		auto integration = std::make_unique<DevelopGuiApplicationTooling>();
		if (!integration->Initialize(createInfo))
		{
			return {};
		}
		return integration;
	}
}
