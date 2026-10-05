#pragma once
#include "ApplicationContentRegistration.h"
#include "AppRuntimeConfig.h"
#include "AppRuntimeHostServices.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "RuntimePaths.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace gglab
{
	class GGLabAppRuntime;
	class RHIContextFactoryBase;
	class InputManager;
	class ApplicationFrameCapture;
	class ApplicationToolingIntegrationBase;
	class PlatformHost;
	class LabRuntimeLocatorBase;
	class LabRuntime;
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
	class DevelopmentShaderHotReloadSystem;
#endif
	struct ApplicationControlRequest;

	namespace win32
	{
		class NamedPipeRequest;
		class NamedPipeServer;
	}
	struct PlatformEvent;
	class Application
	{
	public:
		enum class LifecycleState : uint8_t
		{
			Uninitialized,
			Initializing,
			Running,
			Failed,
			ShuttingDown,
			Stopped,
		};

		struct CreateInfo
		{
			std::wstring_view m_WindowName;
			std::unique_ptr<PlatformHost> m_PlatformHost;
			AppRuntimeConfig m_RuntimeConfig{};
			RuntimePaths m_RuntimePaths{};
			AppRuntimeHostServices m_HostServices{};
			ApplicationContentRegistration m_ContentRegistration{};
			// The main window is never shown or activated and input is ignored.
			bool m_Hidden = false;
			// One capture submitted at startup; the application exits with its result.
			std::optional<FrameCaptureRequest> m_CaptureOnReady;
			double m_CaptureTimeoutSeconds = 120.0;
			// Non-empty serves the session control protocol under this id.
			std::string m_SessionId;
			double m_IdleTimeoutSeconds = 900.0;
		};

	public:
		explicit Application(CreateInfo createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Application);
		~Application() noexcept;

		[[nodiscard]] bool Initialize() noexcept;
		void Run() noexcept;
		void Shutdown() noexcept;
		bool IsInitialized() const noexcept { return m_LifecycleState == LifecycleState::Running; }
		LifecycleState GetLifecycleState() const noexcept { return m_LifecycleState; }

		// Process exit code. Non-zero when startup validation, backend bootstrap,
		// qualification or a running frame fails fatally.
		int GetExitCode() const noexcept { return m_ExitCode; }

		uint32_t GetWindowWidth() const noexcept { return m_WindowWidth; }
		uint32_t GetWindowHeight() const noexcept { return m_WindowHeight; }

	private:
		[[nodiscard]] bool FailInitialization() noexcept;
		bool Tick() noexcept;
		// Paces hidden frames, which present to no visible surface.
		void PaceHiddenFrame() noexcept;
		// Returns false once the capture-on-ready request finished or timed out.
		[[nodiscard]] bool UpdateCaptureOnReady() noexcept;
		[[nodiscard]] bool StartControlSession() noexcept;
		// Answers control requests; returns false when the session should end.
		[[nodiscard]] bool UpdateControlSession() noexcept;
		void HandleControlRequest(const std::shared_ptr<win32::NamedPipeRequest>& pipeRequest,
			const ApplicationControlRequest& request) noexcept;
		void ResolveControlCaptures(std::span<const FrameCaptureRequestResult> results) noexcept;

		void HandlePlatformEvent(const PlatformEvent& event) noexcept;

	private:
		uint32_t m_WindowWidth = 0;
		uint32_t m_WindowHeight = 0;

		std::wstring m_WindowName;
		std::unique_ptr<PlatformHost> m_PlatformHost;
		AppRuntimeConfig m_RuntimeConfig{};
		RuntimePaths m_RuntimePaths{};
		AppRuntimeHostServices m_HostServices{};
		ApplicationContentRegistration m_ContentRegistration{};
		std::unique_ptr<RHIContextFactoryBase> m_RHIContextFactory;
		std::unique_ptr<InputManager> m_InputManager;
		std::unique_ptr<GGLabAppRuntime> m_AppRuntime;
		std::unique_ptr<LabRuntimeLocatorBase> m_LabRuntimeLocator;
		std::unique_ptr<ApplicationToolingIntegrationBase> m_ApplicationTooling;
		std::unique_ptr<ApplicationFrameCapture> m_FrameCapture;
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
		std::unique_ptr<DevelopmentShaderHotReloadSystem> m_ShaderHotReload;
#endif

		std::optional<FrameCaptureRequest> m_CaptureOnReady;
		std::chrono::duration<double> m_CaptureTimeout{ 120.0 };
		uint64_t m_CaptureOnReadyRequestId = 0;
		std::optional<std::chrono::steady_clock::time_point> m_CaptureDeadline;
		std::chrono::steady_clock::time_point m_NextHiddenFrameTime{};

		struct ControlCaptureWait
		{
			std::shared_ptr<win32::NamedPipeRequest> m_PipeRequest;
			uint64_t m_ControlId = 0;
			uint64_t m_CaptureRequestId = 0;
		};

		std::string m_SessionId;
		std::chrono::duration<double> m_IdleTimeout{ 900.0 };
		std::unique_ptr<win32::NamedPipeServer> m_ControlServer;
		std::vector<ControlCaptureWait> m_ControlCaptureWaits;
		// Captures submitted through the control channel: unfinished ids and the
		// most recent finished results, kept for later 'result' queries.
		std::unordered_set<uint64_t> m_ControlCaptureIds;
		std::map<uint64_t, FrameCaptureRequestResult> m_ControlCaptureResults;
		std::chrono::steady_clock::time_point m_StartTime{};
		std::chrono::steady_clock::time_point m_LastControlActivity{};
		bool m_StopRequested = false;

		LifecycleState m_LifecycleState = LifecycleState::Uninitialized;
		bool m_PlatformHostInitializationAttempted = false;
		// Input devices report state regardless of focus; input is read and host
		// shortcuts respond only while the main window is the active window.
		bool m_IsWindowActive = false;
		bool m_Hidden = false;
		bool m_ShutdownComplete = false;
		int m_ExitCode = 0;
	};
}
