#include "GGLabAppRuntime.h"
#include "GGLabTestCore/SelfTest.h"

#include <cstdint>

namespace gglab
{
	namespace
	{
		class FakeBootstrapService final : public AppRuntimeBootstrapServiceBase
		{
		public:
			explicit FakeBootstrapService(bool initializeResult) noexcept :
				m_InitializeResult(initializeResult)
			{}

			[[nodiscard]] bool Initialize() noexcept override
			{
				++m_InitializeCount;
				return m_InitializeResult;
			}

			void Shutdown() noexcept override
			{
				++m_ShutdownCount;
			}

			uint32_t m_InitializeCount = 0;
			uint32_t m_ShutdownCount = 0;

		private:
			bool m_InitializeResult = false;
		};

		void RunLifecycleSelfTests(SelfTestContext& context) noexcept
		{
			FakeBootstrapService service(true);
			{
				GGLabAppRuntime runtime;
				context.Check(
					runtime.GetLifecycleState() == AppRuntimeLifecycleState::Uninitialized,
					"App runtime starts uninitialized");
				context.Check(runtime.Initialize({ .m_BootstrapService = &service }) ==
					AppRuntimeInitializeResult::Succeeded,
					"No-op host service initializes the app runtime");
				context.Check(service.m_InitializeCount == 1 &&
					runtime.Tick() == AppRuntimeTickResult::Continue,
					"Running app runtime returns Continue");

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
				context.Check(runtime.Initialize({ .m_BootstrapService = &failingService }) ==
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
			context.Check(missingServiceRuntime.Initialize({}) ==
				AppRuntimeInitializeResult::MissingBootstrapService &&
				missingServiceRuntime.GetLifecycleState() == AppRuntimeLifecycleState::Failed,
				"Missing required bootstrap service fails atomically");

			FakeBootstrapService untouchedService(true);
			GGLabAppRuntime stoppedRuntime;
			stoppedRuntime.Shutdown();
			stoppedRuntime.Shutdown();
			context.Check(stoppedRuntime.Initialize({ .m_BootstrapService = &untouchedService }) ==
				AppRuntimeInitializeResult::InvalidState &&
				stoppedRuntime.GetLifecycleState() == AppRuntimeLifecycleState::Stopped &&
				untouchedService.m_InitializeCount == 0 && untouchedService.m_ShutdownCount == 0,
				"Stopped runtime cannot reinitialize or touch host services");
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
