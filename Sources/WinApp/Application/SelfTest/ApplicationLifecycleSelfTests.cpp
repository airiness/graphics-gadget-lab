#include "Application/SelfTest/ApplicationLifecycleSelfTests.h"

#include "Application/Application.h"
#include "Application/Content/DesktopApplicationContent.h"
#include "Application/Platform/PlatformHost.h"
#include "Application/Platform/PlatformWindow.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>

namespace gglab
{
	namespace
	{
		struct HostLifetimeProbe
		{
			uint32_t m_InitializeCount = 0;
			uint32_t m_FinalizeCount = 0;
		};

		class FakePlatformWindow final : public PlatformWindow
		{
		public:
			[[nodiscard]] void* GetNativeHandle() const noexcept override { return nullptr; }
			[[nodiscard]] uint32_t GetWidth() const noexcept override { return 640; }
			[[nodiscard]] uint32_t GetHeight() const noexcept override { return 480; }
		};

		class FailingPlatformHost final : public PlatformHost
		{
		public:
			explicit FailingPlatformHost(std::shared_ptr<HostLifetimeProbe> probe) noexcept :
				m_Probe(std::move(probe))
			{}

			[[nodiscard]] bool Initialize(const PlatformWindowCreateInfo&) noexcept override
			{
				++m_Probe->m_InitializeCount;
				return false;
			}

			void Finalize() noexcept override
			{
				++m_Probe->m_FinalizeCount;
			}
			void PumpEvents() noexcept override {}
			[[nodiscard]] bool PollEvent(PlatformEvent&) noexcept override
			{
				return false;
			}
			void WaitForEvents() noexcept override {}
			[[nodiscard]] bool IsQuitRequested() const noexcept override
			{
				return false;
			}
			[[nodiscard]] PlatformWindow& GetMainWindow() noexcept override
			{
				return m_Window;
			}

		private:
			std::shared_ptr<HostLifetimeProbe> m_Probe;
			FakePlatformWindow m_Window;
		};

		Application::CreateInfo MakeCreateInfo(std::unique_ptr<PlatformHost> host) noexcept
		{
			const std::filesystem::path runtimeRoot =
				std::filesystem::temp_directory_path() / "gglab-application-lifecycle-self-test";
			return {
				.m_WindowName = L"ApplicationLifecycleSelfTest",
				.m_PlatformHost = std::move(host),
				.m_RuntimeConfig = {
					.m_RhiBackend = AppRuntimeRHIBackend::DX12,
					.m_StartupDemoId = "test.demo.start",
					.m_InitialExtent = { 640, 480 },
				},
				.m_RuntimePaths = {
					.m_RuntimeRoot = runtimeRoot,
					.m_AssetRoot = runtimeRoot / "Assets",
					.m_ShaderArtifactRoot = runtimeRoot / "ShaderArtifacts",
					.m_IblDerivedDataRoot = runtimeRoot / "DerivedDataCache" / "IBL",
					.m_TextureDerivedDataRoot =
						runtimeRoot / "DerivedDataCache" / "Texture",
					.m_EnvironmentAssetRoot = runtimeRoot / "Assets" / "Textures" / "Skybox",
					.m_SettingsRoot = runtimeRoot,
				},
				.m_ContentRegistration = CreateDesktopApplicationContent(),
			};
		}
	}

	void RunApplicationLifecycleSelfTests(SelfTestContext& context) noexcept
	{
		{
			Application application(MakeCreateInfo(nullptr));
			context.Check(
				application.GetLifecycleState() == Application::LifecycleState::Uninitialized,
				"Application starts with an explicit uninitialized lifecycle state");
			context.Check(!application.Initialize(),
			    "Application rejects initialization without a platform host");
			context.Check(application.GetLifecycleState() == Application::LifecycleState::Failed &&
				application.GetExitCode() == 1,
			    "Missing-host initialization rolls back into a stable failed state");

			application.Shutdown();
			application.Shutdown();
			context.Check(application.GetLifecycleState() == Application::LifecycleState::Failed,
			    "Repeated shutdown preserves the terminal initialization "
			    "failure state");
		}

		const auto probe = std::make_shared<HostLifetimeProbe>();
		{
			Application application(MakeCreateInfo(std::make_unique<FailingPlatformHost>(probe)));
			context.Check(!application.Initialize(),
			    "Application reports a platform-host initialization failure "
			    "to its caller");
			context.Check(probe->m_InitializeCount == 1 && probe->m_FinalizeCount == 1,
			    "A partially initialized platform host is finalized exactly once");

			application.Shutdown();
			application.Shutdown();
			context.Check(probe->m_FinalizeCount == 1,
			    "Explicit repeated shutdown does not finalize the platform host twice");
			context.Check(!application.Initialize(),
			    "A failed Application lifetime cannot be silently reinitialized");
		}
		context.Check(probe->m_FinalizeCount == 1,
			"Application destructor fallback remains idempotent after explicit shutdown");

		const auto stoppedProbe = std::make_shared<HostLifetimeProbe>();
		{
			Application application(
				MakeCreateInfo(std::make_unique<FailingPlatformHost>(stoppedProbe)));
			application.Shutdown();
			application.Shutdown();
			context.Check(application.GetLifecycleState() == Application::LifecycleState::Stopped,
			    "Shutdown before initialization produces a stable stopped state");
			context.Check(
				stoppedProbe->m_InitializeCount == 0 && stoppedProbe->m_FinalizeCount == 0,
				"Shutdown before initialization does not touch an unstarted platform host");
		}
	}
}
