#include "GGLabAppRuntime.h"
#include "ApplicationFrameworkSelfTests.h"
#include "ApplicationInput.h"
#include "ApplicationToolingIntegration.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/RHI/RHIContext.h"

#include <filesystem>

namespace gglab
{
	namespace
	{
		class NullRHIContextFactory final : public RHIContextFactoryBase
		{
		public:
			std::unique_ptr<RHIContext> CreateContext(
				const RHIContextDesc&) const noexcept override
			{
				return nullptr;
			}
		};

		std::unique_ptr<DemoBase> CreateLifecycleTestDemo(const DemoCreateInfo&,
			const LabId&, std::span<const LabRegistration>) noexcept
		{
			return nullptr;
		}

		ApplicationContentRegistration MakeContentRegistration()
		{
			ApplicationContentRegistration registration;
			registration.m_Demos.push_back({
				.m_Id = "test.demo.start",
				.m_Factory = &CreateLifecycleTestDemo,
				});
			return registration;
		}

		class RecordingApplicationTooling final : public ApplicationToolingIntegrationBase
		{
		public:
			ApplicationToolingInputCapture GetPreviousFrameInputCapture()
				const noexcept override
			{
				return {};
			}

			void ResolveFrameSettings(const ViewRenderProfile&,
				ShadowVisualizationSettings&, ViewRenderProfile&) const noexcept override
			{
			}

			bool BeginFrame() noexcept override
			{
				++m_BeginCount;
				return m_BeginSucceeds;
			}

			void Draw(const ApplicationToolingFrameContext&) noexcept override { ++m_DrawCount; }

			void EndFrame(ApplicationToolingFrameEndReason reason) noexcept override
			{
				++m_EndCount;
				m_LastEndReason = reason;
			}

			RenderPipelineOverlayExtensionBase* GetOverlayExtension() noexcept override
			{
				return m_HasOverlay
					? reinterpret_cast<RenderPipelineOverlayExtensionBase*>(this)
					: nullptr;
			}

			bool m_BeginSucceeds = true;
			bool m_HasOverlay = true;
			uint32_t m_BeginCount = 0;
			uint32_t m_DrawCount = 0;
			uint32_t m_EndCount = 0;
			ApplicationToolingFrameEndReason m_LastEndReason =
				ApplicationToolingFrameEndReason::Completed;
		};

		void RunApplicationToolingSelfTests(SelfTestContext& context) noexcept
		{
			{
				ApplicationToolingFrame frame(nullptr);
				frame.Draw({});
				frame.Complete();
				context.Check(!frame.IsOpen() && frame.GetOverlayExtension() == nullptr,
					"Disabled or failed optional tooling opens no frame and needs no closure");
			}

			{
				RecordingApplicationTooling tooling;
				tooling.m_BeginSucceeds = false;
				ApplicationToolingFrame frame(&tooling);
				frame.Draw({});
				context.Check(tooling.m_BeginCount == 1 && tooling.m_DrawCount == 0 &&
					tooling.m_EndCount == 0 && !frame.IsOpen(),
					"Tooling BeginFrame failure is never paired with a false closure");
			}

			{
				RecordingApplicationTooling tooling;
				{
					ApplicationToolingFrame frame(&tooling);
					frame.Draw({});
				}
				context.Check(tooling.m_BeginCount == 1 && tooling.m_DrawCount == 1 &&
					tooling.m_EndCount == 1 &&
					tooling.m_LastEndReason == ApplicationToolingFrameEndReason::Aborted,
					"Early frame failure aborts one successfully opened tooling frame exactly once");
			}

			{
				RecordingApplicationTooling tooling;
				ApplicationToolingFrame frame(&tooling);
				const bool exposedOverlay = frame.GetOverlayExtension() != nullptr;
				frame.Draw({});
				frame.Complete();
				frame.Complete();
				context.Check(exposedOverlay && tooling.m_DrawCount == 1 &&
					tooling.m_EndCount == 1 &&
					tooling.m_LastEndReason == ApplicationToolingFrameEndReason::Completed &&
					frame.GetOverlayExtension() == nullptr,
					"Normal completion closes once and retires the frame overlay extension");
			}

			{
				RecordingApplicationTooling tooling;
				tooling.m_HasOverlay = false;
				ApplicationToolingFrame frame(&tooling);
				frame.Complete();
				context.Check(tooling.m_EndCount == 1 &&
					tooling.m_LastEndReason == ApplicationToolingFrameEndReason::Completed,
					"A pipeline without an overlay pass still completes its tooling frame once");
			}

			{
				RecordingApplicationTooling tooling;
				ApplicationToolingFrame frame(&tooling);
				frame.Abort();
				frame.Abort();
				context.Check(tooling.m_EndCount == 1 &&
					tooling.m_LastEndReason == ApplicationToolingFrameEndReason::Aborted,
					"Explicit render failure abort is idempotent");
			}
		}

		void RunApplicationInputSelfTests(SelfTestContext& context) noexcept
		{
			ApplicationInput input;
			context.Check(!input.IsAvailable() &&
				!input.IsKeyHeld(AppInputKey::W) &&
				!input.IsPointerButtonHeld(AppPointerButton::Left),
				"Unavailable input starts as a safe all-released fallback");

			ApplicationInputSnapshot pressed{};
			pressed.m_IsAvailable = true;
			pressed.m_KeysHeld[static_cast<size_t>(AppInputKey::W)] = true;
			pressed.m_PointerButtonsHeld[static_cast<size_t>(AppPointerButton::Left)] = true;
			pressed.m_PointerPosition = { 320.0f, 180.0f };
			pressed.m_PointerDelta = { 4.0f, -2.0f };
			pressed.m_ScrollDeltaY = 120;
			input.Publish(pressed);
			context.Check(input.IsAvailable() && input.IsKeyPressed(AppInputKey::W) &&
				input.IsKeyHeld(AppInputKey::W) &&
				input.IsPointerButtonPressed(AppPointerButton::Left) &&
				input.GetPointerDelta().m_X == 4.0f && input.GetScrollDeltaY() == 120,
				"Published state derives key, pointer, and scroll transitions");

			input.Publish(pressed);
			context.Check(!input.IsKeyPressed(AppInputKey::W) &&
				input.IsKeyHeld(AppInputKey::W) &&
				!input.IsPointerButtonPressed(AppPointerButton::Left),
				"Held state does not repeat pressed transitions");

			ApplicationInputSnapshot released = pressed;
			released.m_KeysHeld[static_cast<size_t>(AppInputKey::W)] = false;
			released.m_PointerButtonsHeld[static_cast<size_t>(AppPointerButton::Left)] = false;
			input.Publish(released);
			context.Check(input.IsKeyReleased(AppInputKey::W) &&
				input.IsPointerButtonReleased(AppPointerButton::Left),
				"Released transitions are derived from consecutive snapshots");

			input.SetUICaptureState(true, true);
			context.Check(input.IsKeyboardCapturedByUI() && input.IsPointerCapturedByUI(),
				"UI capture is explicit neutral routing state");
			input.SetPointerMode(AppPointerMode::Absolute);
			context.Check(input.GetPointerMode() == AppPointerMode::Absolute &&
				input.GetPointerDelta().m_X == 0.0f &&
				input.GetPointerPosition().m_X == 320.0f,
				"Pointer mode transition clears relative motion and preserves position");

			input.Reset();
			context.Check(!input.IsAvailable() && !input.IsKeyReleased(AppInputKey::W) &&
				!input.IsKeyboardCapturedByUI() && !input.IsPointerCapturedByUI() &&
				input.GetPointerMode() == AppPointerMode::Absolute,
				"Lifecycle reset clears transient state while preserving requested pointer mode");
			context.Check(!input.IsKeyHeld(static_cast<AppInputKey>(255)) &&
				!input.IsPointerButtonHeld(static_cast<AppPointerButton>(255)),
				"Invalid neutral input codes are rejected safely");
		}

		[[nodiscard]] GGLabAppRuntimeCreateInfo MakeCreateInfo() noexcept
		{
			const std::filesystem::path runtimeRoot =
				std::filesystem::temp_directory_path() / "gglab-app-runtime-test";
			return {
				.m_Config = {
					.m_RhiBackend = AppRuntimeRHIBackend::DX12,
					.m_StartupDemoId = "test.demo.start",
					.m_InitialExtent = { 1280, 720 },
				},
				.m_Paths = {
					.m_RuntimeRoot = runtimeRoot,
					.m_AssetRoot = runtimeRoot / "Assets",
					.m_ShaderSourceRoot = runtimeRoot / "Shaders",
					.m_ShaderCacheRoot = runtimeRoot / "ShaderCache",
					.m_IblDerivedDataRoot = runtimeRoot / "DerivedDataCache" / "IBL",
					.m_TextureDerivedDataRoot =
						runtimeRoot / "DerivedDataCache" / "Texture",
					.m_EnvironmentAssetRoot = runtimeRoot / "Assets" / "Textures" / "Skybox",
					.m_SettingsRoot = runtimeRoot,
				},
			};
		}

		void RunLifecycleSelfTests(SelfTestContext& context) noexcept
		{
			RunApplicationFrameworkSelfTests(context);
			RunApplicationInputSelfTests(context);
			RunApplicationToolingSelfTests(context);
			{
				GGLabAppRuntime runtime;
				context.Check(
					runtime.GetLifecycleState() == AppRuntimeLifecycleState::Uninitialized,
					"App runtime starts uninitialized");
				context.Check(runtime.Initialize(MakeCreateInfo()) ==
					AppRuntimeInitializeResult::Succeeded,
					"Valid explicit config and paths initialize the app runtime");
				context.Check(runtime.Initialize(MakeCreateInfo()) ==
					AppRuntimeInitializeResult::AlreadyInitialized &&
					runtime.Tick() == AppRuntimeTickResult::Continue,
					"Running app runtime rejects repeated initialization");
				RecordingApplicationTooling uncomposedTooling;
				context.Check(runtime.Tick({
					.m_ApplicationTooling = &uncomposedTooling,
					}) == AppRuntimeTickResult::Continue &&
					uncomposedTooling.m_BeginCount == 0,
					"A lifecycle-only runtime never opens a production tooling frame");

				runtime.HandleHostEvent(AppHostEventType::Suspended);
				context.Check(runtime.GetLifecycleState() == AppRuntimeLifecycleState::Suspended &&
					runtime.Tick() == AppRuntimeTickResult::Suspended,
					"Suspended host state returns Suspended without blocking");
				runtime.HandleHostEvent(AppHostEventType::Resumed);
				context.Check(runtime.Tick() == AppRuntimeTickResult::Continue,
					"Resumed host state returns Continue");
				runtime.HandleHostEvent(AppHostEventType::ExitRequested);
				context.Check(runtime.GetLifecycleState() ==
					AppRuntimeLifecycleState::ExitRequested &&
					runtime.Tick() == AppRuntimeTickResult::Exit,
					"Host exit request produces a non-blocking Exit result");

				runtime.Shutdown();
				runtime.Shutdown();
				context.Check(runtime.GetLifecycleState() == AppRuntimeLifecycleState::Stopped,
					"Repeated shutdown is idempotent");
			}

			GGLabAppRuntime stoppedRuntime;
			stoppedRuntime.Shutdown();
			stoppedRuntime.Shutdown();
			context.Check(stoppedRuntime.Initialize(MakeCreateInfo()) ==
				AppRuntimeInitializeResult::InvalidState &&
				stoppedRuntime.GetLifecycleState() == AppRuntimeLifecycleState::Stopped,
				"Stopped runtime cannot reinitialize");

			GGLabAppRuntimeCreateInfo invalidConfig = MakeCreateInfo();
			invalidConfig.m_Config.m_InitialExtent.m_Width = 0;
			GGLabAppRuntime invalidConfigRuntime;
			context.Check(invalidConfigRuntime.Initialize(invalidConfig) ==
				AppRuntimeInitializeResult::InvalidConfig &&
				invalidConfigRuntime.GetLifecycleState() == AppRuntimeLifecycleState::Failed,
				"Invalid shared config fails atomically");

			GGLabAppRuntimeCreateInfo missingStartupDemo = MakeCreateInfo();
			missingStartupDemo.m_Config.m_StartupDemoId.clear();
			GGLabAppRuntime missingStartupDemoRuntime;
			context.Check(missingStartupDemoRuntime.Initialize(missingStartupDemo) ==
				AppRuntimeInitializeResult::InvalidConfig,
				"Shared runtime config requires an explicit host-selected startup Demo ID");

			GGLabAppRuntimeCreateInfo invalidPaths = MakeCreateInfo();
			invalidPaths.m_Paths.m_AssetRoot.clear();
			GGLabAppRuntime invalidPathsRuntime;
			context.Check(invalidPathsRuntime.Initialize(invalidPaths) ==
				AppRuntimeInitializeResult::InvalidRuntimePaths &&
				invalidPathsRuntime.GetLifecycleState() == AppRuntimeLifecycleState::Failed,
				"Invalid runtime paths fail atomically");

			GGLabAppRuntime invalidServiceCreateInfoRuntime;
			GGLAB_UNUSED(invalidServiceCreateInfoRuntime.Initialize(MakeCreateInfo()));
			context.Check(invalidServiceCreateInfoRuntime.InitializeServices({}) ==
				AppRuntimeServiceInitializeResult::InvalidCreateInfo &&
				invalidServiceCreateInfoRuntime.GetLifecycleState() ==
				AppRuntimeLifecycleState::Failed,
				"Invalid service composition fails atomically before creating runtime services");

			NullRHIContextFactory nullRhiContextFactory;
			ApplicationInput input;
			GGLabAppRuntime invalidContentRuntime;
			GGLAB_UNUSED(invalidContentRuntime.Initialize(MakeCreateInfo()));
			context.Check(invalidContentRuntime.InitializeServices({
				.m_RHIContextFactory = &nullRhiContextFactory,
				.m_Input = &input,
				.m_WindowWidth = 640,
				.m_WindowHeight = 480,
				}) == AppRuntimeServiceInitializeResult::InvalidContentRegistration &&
				!invalidContentRuntime.AreServicesInitialized(),
				"Service composition rejects an invalid content registration before RHI work");

			GGLabAppRuntime rendererFailureRuntime;
			GGLAB_UNUSED(rendererFailureRuntime.Initialize(MakeCreateInfo()));
			context.Check(rendererFailureRuntime.InitializeServices({
				.m_RHIContextFactory = &nullRhiContextFactory,
				.m_Input = &input,
				.m_ContentRegistration = MakeContentRegistration(),
				.m_WindowWidth = 640,
				.m_WindowHeight = 480,
				}) == AppRuntimeServiceInitializeResult::RendererInitializationFailed &&
				rendererFailureRuntime.GetLifecycleState() ==
				AppRuntimeLifecycleState::Failed &&
				!rendererFailureRuntime.AreServicesInitialized(),
				"Renderer creation failure tears down partially composed runtime services");
		}
	}
}

int main()
{
	gglab::ConsoleSelfTestReporter reporter;
	return gglab::RunSelfTestSuite({
		.m_Id = "app-runtime-lifecycle",
		.m_Run = &gglab::RunLifecycleSelfTests,
		}, reporter)
		? 0
		: 1;
}
