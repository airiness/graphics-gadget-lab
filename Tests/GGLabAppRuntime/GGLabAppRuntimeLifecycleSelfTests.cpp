#include "GGLabAppRuntime.h"
#include "ApplicationInput.h"
#include "GGLabTestCore/SelfTest.h"

#include <cstdint>
#include <filesystem>

namespace gglab
{
	namespace
	{
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

		class FakeBootstrapService final : public AppRuntimeBootstrapServiceBase
		{
		public:
			explicit FakeBootstrapService(bool initializeResult) noexcept :
				m_InitializeResult(initializeResult)
			{}

			[[nodiscard]] bool Initialize(const AppRuntimeConfig& config,
				const RuntimePaths& paths,
				const AppRuntimeHostServices& hostServices) noexcept override
			{
				++m_InitializeCount;
				m_ReceivedValidConfig = config.IsValid();
				m_ReceivedValidPaths = paths.IsValid();
				m_ReceivedWorkerLifecycle = hostServices.m_TaskWorkerLifecycle != nullptr;
				return m_InitializeResult;
			}

			void Shutdown() noexcept override
			{
				++m_ShutdownCount;
			}

			uint32_t m_InitializeCount = 0;
			uint32_t m_ShutdownCount = 0;
			bool m_ReceivedValidConfig = false;
			bool m_ReceivedValidPaths = false;
			bool m_ReceivedWorkerLifecycle = false;

		private:
			bool m_InitializeResult = false;
		};

		[[nodiscard]] GGLabAppRuntimeCreateInfo MakeCreateInfo(
			AppRuntimeBootstrapServiceBase* service) noexcept
		{
			const std::filesystem::path runtimeRoot =
				std::filesystem::temp_directory_path() / "gglab-app-runtime-test";
			return {
				.m_Config = {
					.m_RhiBackend = AppRuntimeRHIBackend::DX12,
					.m_InitialExtent = { 1280, 720 },
					.m_Capabilities = AppRuntimeCapability::BuiltInContent,
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
				.m_BootstrapService = service,
			};
		}

		void RunLifecycleSelfTests(SelfTestContext& context) noexcept
		{
			RunApplicationInputSelfTests(context);
			FakeBootstrapService service(true);
			{
				GGLabAppRuntime runtime;
				context.Check(
					runtime.GetLifecycleState() == AppRuntimeLifecycleState::Uninitialized,
					"App runtime starts uninitialized");
				context.Check(runtime.Initialize(MakeCreateInfo(&service)) ==
					AppRuntimeInitializeResult::Succeeded,
					"No-op host service initializes the app runtime");
				context.Check(service.m_InitializeCount == 1 && service.m_ReceivedValidConfig &&
					service.m_ReceivedValidPaths && !service.m_ReceivedWorkerLifecycle &&
					runtime.Tick() == AppRuntimeTickResult::Continue,
					"Bootstrap receives explicit valid config, paths, and optional host services");

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
				context.Check(runtime.GetLifecycleState() == AppRuntimeLifecycleState::Stopped &&
					service.m_ShutdownCount == 1,
					"Repeated shutdown releases initialized services exactly once");
			}
			context.Check(service.m_ShutdownCount == 1,
				"Destructor fallback is idempotent after explicit shutdown");

			FakeBootstrapService failingService(false);
			{
				GGLabAppRuntime runtime;
				context.Check(runtime.Initialize(MakeCreateInfo(&failingService)) ==
					AppRuntimeInitializeResult::BootstrapServiceFailed,
					"Bootstrap service failure is reported to the host");
				context.Check(runtime.GetLifecycleState() == AppRuntimeLifecycleState::Failed &&
					failingService.m_InitializeCount == 1 &&
					failingService.m_ShutdownCount == 1,
					"Partial initialization failure rolls back exactly once");
				runtime.Shutdown();
				context.Check(failingService.m_ShutdownCount == 1,
					"Repeated shutdown preserves the failed terminal state");
			}
			context.Check(failingService.m_ShutdownCount == 1,
				"Failed runtime destructor does not repeat rollback");

			GGLabAppRuntime missingServiceRuntime;
			context.Check(missingServiceRuntime.Initialize(MakeCreateInfo(nullptr)) ==
				AppRuntimeInitializeResult::MissingBootstrapService &&
				missingServiceRuntime.GetLifecycleState() == AppRuntimeLifecycleState::Failed,
				"Missing required bootstrap service fails atomically");

			FakeBootstrapService untouchedService(true);
			GGLabAppRuntime stoppedRuntime;
			stoppedRuntime.Shutdown();
			stoppedRuntime.Shutdown();
			context.Check(stoppedRuntime.Initialize(MakeCreateInfo(&untouchedService)) ==
				AppRuntimeInitializeResult::InvalidState &&
				stoppedRuntime.GetLifecycleState() == AppRuntimeLifecycleState::Stopped &&
				untouchedService.m_InitializeCount == 0 && untouchedService.m_ShutdownCount == 0,
				"Stopped runtime cannot reinitialize or touch host services");

			FakeBootstrapService invalidInputService(true);
			GGLabAppRuntimeCreateInfo invalidConfig = MakeCreateInfo(&invalidInputService);
			invalidConfig.m_Config.m_InitialExtent.m_Width = 0;
			GGLabAppRuntime invalidConfigRuntime;
			context.Check(invalidConfigRuntime.Initialize(invalidConfig) ==
				AppRuntimeInitializeResult::InvalidConfig &&
				invalidInputService.m_InitializeCount == 0 &&
				invalidInputService.m_ShutdownCount == 0,
				"Invalid shared config fails before touching host services");

			GGLabAppRuntimeCreateInfo invalidPaths = MakeCreateInfo(&invalidInputService);
			invalidPaths.m_Paths.m_AssetRoot.clear();
			GGLabAppRuntime invalidPathsRuntime;
			context.Check(invalidPathsRuntime.Initialize(invalidPaths) ==
				AppRuntimeInitializeResult::InvalidRuntimePaths &&
				invalidInputService.m_InitializeCount == 0 &&
				invalidInputService.m_ShutdownCount == 0,
				"Invalid runtime paths fail before touching host services");
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
