#include "ApplicationLifecycleSelfTests.h"

#include "Application/Application.h"
#include "Application/Content/DesktopApplicationContent.h"
#include "Application/Platform/PlatformHost.h"
#include "Application/Platform/PlatformWindow.h"
#include "Application/Control/ApplicationControlCaptureJobs.h"
#include "Application/Platform/Windows/Win32NamedPipeServer.h"
#include "Application/Platform/Windows/Win32PlatformHost.h"
#include "GGLabAppRuntime.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <windows.h>

namespace gglab
{
	struct ApplicationLifecycleTestAccess
	{
		static bool StartSuspended(Application& application) noexcept
		{
			application.m_AppRuntime = std::make_unique<GGLabAppRuntime>();
			if (application.m_AppRuntime->Initialize({
				.m_Config = application.m_RuntimeConfig,
				.m_Paths = application.m_RuntimePaths }) != AppRuntimeInitializeResult::Succeeded)
			{
				return false;
			}
			application.m_PlatformHostInitializationAttempted = true;
			if (!application.m_PlatformHost->Initialize({}))
			{
				return false;
			}
			application.m_StartTime = std::chrono::steady_clock::now();
			if (!application.StartControlSession())
			{
				return false;
			}
			application.m_LifecycleState = Application::LifecycleState::Running;
			application.HandlePlatformEvent({ .m_Type = PlatformEventType::Suspended });
			return application.m_AppRuntime->GetLifecycleState() == AppRuntimeLifecycleState::Suspended;
		}
	};

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
			{
			}

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
			void Wake() noexcept override {}
			void WaitForEvents(std::optional<std::chrono::steady_clock::time_point>) noexcept override {}
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

		class SuspendedPlatformHost final : public PlatformHost
		{
		public:
			bool Initialize(const PlatformWindowCreateInfo&) noexcept override { return true; }
			void Finalize() noexcept override {}
			void PumpEvents() noexcept override {}
			bool PollEvent(PlatformEvent&) noexcept override { return false; }
			bool IsQuitRequested() const noexcept override
			{
				return std::chrono::steady_clock::now() >= m_TestDeadline;
			}
			PlatformWindow& GetMainWindow() noexcept override { return m_Window; }
			void Wake() noexcept override
			{
				std::scoped_lock lock(m_Mutex);
				m_WakePending = true;
				m_Condition.notify_one();
			}
			void WaitForEvents(std::optional<std::chrono::steady_clock::time_point> deadline) noexcept override
			{
				std::unique_lock lock(m_Mutex);
				m_Condition.wait_until(lock, std::min(deadline.value_or(m_TestDeadline), m_TestDeadline),
					[this]() { return m_WakePending; });
				m_WakePending = false;
			}

		private:
			FakePlatformWindow m_Window;
			std::mutex m_Mutex;
			std::condition_variable m_Condition;
			bool m_WakePending = false;
			std::chrono::steady_clock::time_point m_TestDeadline =
				std::chrono::steady_clock::now() + std::chrono::seconds(3);
		};

		std::string RequestSession(const std::wstring& pipeName, std::string_view command) noexcept
		{
			HANDLE pipe = INVALID_HANDLE_VALUE;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
			while (pipe == INVALID_HANDLE_VALUE && std::chrono::steady_clock::now() < deadline)
			{
				pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE,
					0, nullptr, OPEN_EXISTING, 0, nullptr);
				if (pipe == INVALID_HANDLE_VALUE)
				{
					WaitNamedPipeW(pipeName.c_str(), 50);
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
			if (pipe == INVALID_HANDLE_VALUE)
			{
				return {};
			}
			const std::string request = std::format(
				R"({{"protocol":4,"id":1,"command":"{}"{}}})", command,
				command == "capture-cancel" || command == "result" ? ",\"jobId\":1" : "") + '\n';
			DWORD bytes = 0;
			std::string response;
			if (WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()), &bytes, nullptr))
			{
				char buffer[4096];
				while (ReadFile(pipe, buffer, sizeof(buffer), &bytes, nullptr) && bytes)
				{
					response.append(buffer, bytes);
					if (response.find('\n') != std::string::npos)
					{
						break;
					}
				}
			}
			CloseHandle(pipe);
			return response;
		}

		Application::CreateInfo MakeCreateInfo(std::unique_ptr<PlatformHost> host) noexcept
		{
			const std::filesystem::path runtimeRoot = std::filesystem::temp_directory_path() / "gglab-application-lifecycle-self-test";
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

		void RunSuspendedSessionTests(SelfTestContext& context) noexcept
		{
			using Json = nlohmann::json;
			auto info = MakeCreateInfo(std::make_unique<SuspendedPlatformHost>());
			info.m_SessionId = std::format("suspended-{}-{}", GetCurrentProcessId(),
				std::chrono::steady_clock::now().time_since_epoch().count());
			info.m_IdleTimeoutSeconds = 2.0;
			const std::wstring pipeName = L"\\\\.\\pipe\\gglab-session-" +
				std::wstring(info.m_SessionId.begin(), info.m_SessionId.end());
			Application application(std::move(info));
			const bool started = ApplicationLifecycleTestAccess::StartSuspended(application);
			context.Check(started, "A host-injected Suspended event holds the session without GPU services");
			if (!started)
			{
				return;
			}
			Json status, cancelled, sequence, result, stopped;
			std::thread client([&]()
				{
					status = Json::parse(RequestSession(pipeName, "status"), nullptr, false);
					cancelled = Json::parse(RequestSession(pipeName, "capture-cancel"), nullptr, false);
					sequence = Json::parse(RequestSession(pipeName, "sequence-cancel"), nullptr, false);
					result = Json::parse(RequestSession(pipeName, "result"), nullptr, false);
					stopped = Json::parse(RequestSession(pipeName, "stop"), nullptr, false);
				});
			const auto began = std::chrono::steady_clock::now();
			application.Run();
			application.Shutdown();
			client.join();
			context.Check(status.is_object() && status.value("ok", false) &&
				status["session"]["runtimeState"] == "suspended" && status["frame"].is_null(),
				"Suspended status answers with its lifecycle state without a rendered frame");
			context.Check(cancelled.is_object() && cancelled["cancellation"] == "NotFound" &&
				sequence.is_object() && result.is_object(),
				"Cancellation and result queries remain responsive while suspended");
			context.Check(stopped.is_object() && stopped.value("ok", false) &&
				application.GetLifecycleState() == Application::LifecycleState::Stopped &&
				std::chrono::steady_clock::now() - began < std::chrono::seconds(2),
				"A suspended stop wakes the owner and completes normal shutdown within a bounded time");

			auto idleInfo = MakeCreateInfo(std::make_unique<SuspendedPlatformHost>());
			idleInfo.m_SessionId = std::format("idle-{}", GetCurrentProcessId());
			idleInfo.m_IdleTimeoutSeconds = 0.04;
			Application idle(std::move(idleInfo));
			const bool idleStarted = ApplicationLifecycleTestAccess::StartSuspended(idle);
			context.Check(idleStarted, "The idle-expiry fixture starts suspended");
			if (idleStarted)
			{
				const auto idleBegan = std::chrono::steady_clock::now();
				idle.Run();
				const auto elapsed = std::chrono::steady_clock::now() - idleBegan;
				idle.Shutdown();
				context.Check(elapsed >= std::chrono::milliseconds(30) && elapsed < std::chrono::seconds(1),
					"An abandoned suspended automation session wakes at its idle deadline without pipe traffic");
			}
		}

		void RunPlatformWakeTests(SelfTestContext& context) noexcept
		{
			Win32PlatformHost host(GetModuleHandleW(nullptr));
			const bool initialized = host.Initialize({
				.m_Title = L"PlatformWakeSelfTest", .m_Width = 64, .m_Height = 64, .m_Hidden = true });
			context.Check(initialized, "A hidden platform window supports control wakes without an RHI");
			if (initialized)
			{
				host.PumpEvents();
				host.Wake();
				const auto began = std::chrono::steady_clock::now();
				host.WaitForEvents(began + std::chrono::seconds(1));
				context.Check(std::chrono::steady_clock::now() - began < std::chrono::milliseconds(200),
					"A request wake queued before the native wait is not lost");
				host.PumpEvents();
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(30);
				do
				{
					host.WaitForEvents(deadline);
					host.PumpEvents();
				} while (std::chrono::steady_clock::now() < deadline);
				context.Check(std::chrono::steady_clock::now() < deadline + std::chrono::milliseconds(200),
					"The native event wait wakes for an absolute control deadline without a window message");
			}
			host.Finalize();
		}
	}

	void RunApplicationLifecycleSelfTests(SelfTestContext& context) noexcept
	{
		RunPlatformWakeTests(context);
		RunSuspendedSessionTests(context);
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
			Application application(MakeCreateInfo(std::make_unique<FailingPlatformHost>(stoppedProbe)));
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
