#pragma once
#include "ApplicationContentRegistration.h"
#include "AppRuntimeConfig.h"
#include "AppRuntimeHostServices.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "RuntimePaths.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace gglab
{
	class GGLabAppRuntime;
	class RHIContextFactoryBase;
	class InputManager;
	class ApplicationToolingIntegrationBase;
	class PlatformHost;
	class LabRuntimeLocatorBase;
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
	class DevelopmentShaderHotReloadSystem;
#endif
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
		// or qualification fails.
		int GetExitCode() const noexcept { return m_ExitCode; }

		uint32_t GetWindowWidth() const noexcept { return m_WindowWidth; }
		uint32_t GetWindowHeight() const noexcept { return m_WindowHeight; }

	private:
		[[nodiscard]] bool FailInitialization() noexcept;
		bool Tick() noexcept;

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
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
		std::unique_ptr<DevelopmentShaderHotReloadSystem> m_ShaderHotReload;
#endif

		LifecycleState m_LifecycleState = LifecycleState::Uninitialized;
		bool m_PlatformHostInitializationAttempted = false;
		bool m_ShutdownComplete = false;
		int m_ExitCode = 0;
	};
}
