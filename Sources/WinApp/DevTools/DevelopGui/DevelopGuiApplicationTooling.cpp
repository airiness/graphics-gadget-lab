#include "Application/Tooling/ApplicationToolingComposition.h"
#include "Application/Demo/DemoLabRuntimeLocator.h"
#include "Demo/DemoManager.h"
#include "Lab/LabRuntime.h"
#include "ApplicationToolingIntegration.h"
#include "GGLabRuntime/Graphics/CameraRig.h"
#include "GGLabRuntime/Graphics/RHI/DX12/DX12ResourceLifecycleTools.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiSystem.h"
#include "DevTools/DevelopGui/LoadingOverlay.h"
#include "DevTools/DevelopGui/Panels/DemoPanel.h"
#include "DevTools/DevelopGui/Panels/LabPanel.h"

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
				PrepareForShutdown();
			}

			void PrepareForShutdown() noexcept override
			{
				m_ResourceLifecycleTools.reset();
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

				m_ResourceLifecycleTools = CreateDX12ResourceLifecycleTools(*createInfo.m_RHIContext);
				auto& runtime = m_System.GetDevToolsRuntime();
				runtime.GetRegistry().RegisterPanel(
					std::make_unique<DemoPanel>(createInfo.m_DemoManager));
				if (createInfo.m_LabRuntimeLocator)
				{
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

			ApplicationToolingFrameSettingsResolution ResolveFrameSettings(
				const ViewRenderProfile& authoringProfile,
				ShadowVisualizationSettings& outShadowVisualizationSettings,
				ViewRenderProfile& outEffectiveProfile) const noexcept override
			{
				const auto& runtime = m_System.GetDevToolsRuntime();
				outShadowVisualizationSettings =
					runtime.GetRenderVisualizationSettings().m_Shadow;
				outEffectiveProfile = runtime.ResolveViewRenderProfile(authoringProfile);
				return {
					.m_GTAOOverrideActive =
						runtime.GetViewRenderSettingsOverrides().m_GTAO.m_IsActive,
				};
			}

			bool BeginFrame() noexcept override { return m_System.BeginFrame(); }

			void Draw(const ApplicationToolingFrameContext& context) noexcept override
			{
				DevelopGuiContext guiContext{};
				guiContext.m_CameraRig = context.m_CameraRig;
				guiContext.m_CameraRenderViewQuery = context.m_CameraRig;
				guiContext.m_DX12ResourceLifecycle = m_ResourceLifecycleTools.get();
				guiContext.m_DX12ResourceLifecycleControl = m_ResourceLifecycleTools.get();
				guiContext.m_World = context.m_World;
				guiContext.m_DirectionalLight = context.m_DirectionalLight;
				guiContext.m_DirectionalLightControl = context.m_DirectionalLightControl;
				guiContext.m_AssetManager = context.m_AssetManager;
				guiContext.m_EnvironmentSelectionControl =
					context.m_EnvironmentSelectionControl;
				guiContext.m_Diagnostics = context.m_Diagnostics;
				guiContext.m_DiagnosticsControl = context.m_DiagnosticsControl;
				guiContext.m_EnvironmentLighting = context.m_EnvironmentLighting;
				guiContext.m_EnvironmentLightingControl = context.m_EnvironmentLightingControl;
				guiContext.m_GpuProfiling = context.m_GpuProfiling;
				guiContext.m_GpuProfilingControl = context.m_GpuProfilingControl;
				guiContext.m_IBLCacheControl = context.m_IBLCacheControl;
				guiContext.m_IBLPreview = context.m_IBLPreview;
				guiContext.m_IBLPreviewControl = context.m_IBLPreviewControl;
				guiContext.m_PostProcessPreview = context.m_PostProcessPreview;
				guiContext.m_PostProcessPreviewControl = context.m_PostProcessPreviewControl;
				guiContext.m_ShadowPreview = context.m_ShadowPreview;
				guiContext.m_DebugDrawSystem = context.m_DebugDrawSystem;
				guiContext.m_DebugDrawFrame =
					context.m_DebugDrawFrame ? *context.m_DebugDrawFrame : DebugDrawFrameView{};

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
			std::unique_ptr<DX12ResourceLifecycleToolsBase> m_ResourceLifecycleTools;
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
