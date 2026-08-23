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
	class Renderer;
	class GGLabAppRuntime;
	class RHIContextFactoryBase;
	class AssetManager;
	class EnvironmentAssetController;
	class TaskSystem;
	class InputManager;
	class ApplicationInput;
	class ShaderManager;
	class DemoManager;
	class RenderFrameBuilder;
	class ApplicationToolingIntegrationBase;
	class DebugDrawSystem;
	class PlatformHost;
	class Time;
	class World;
	class LabRuntimeLocatorBase;
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

		Renderer* GetRenderer() const noexcept { return m_Renderer; }
		AssetManager* GetAssetManager() const noexcept { return m_AssetManager; }
		EnvironmentAssetController* GetEnvironmentAssetController() const noexcept
		{
			return m_EnvironmentAssetController;
		}
		TaskSystem* GetTaskSystem() const noexcept { return m_TaskSystem; }
		ApplicationInput* GetInput() const noexcept;
		ShaderManager* GetShaderManager() const noexcept { return m_ShaderManager; }

		Time* GetTime() const noexcept { return m_Time; }

	private:
		[[nodiscard]] bool FailInitialization() noexcept;
		bool Tick() noexcept;

		void HandlePlatformEvent(const PlatformEvent& event) noexcept;

		// Platform lifecycle handlers
		void OnActive() noexcept;
		void OnInactive() noexcept;
		void OnSuspend() noexcept;
		void OnResume() noexcept;
		void OnResize(uint32_t width, uint32_t height) noexcept;

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
		Renderer* m_Renderer = nullptr;
		Time* m_Time = nullptr;
		TaskSystem* m_TaskSystem = nullptr;
		AssetManager* m_AssetManager = nullptr;
		EnvironmentAssetController* m_EnvironmentAssetController = nullptr;
		ShaderManager* m_ShaderManager = nullptr;
		DemoManager* m_DemoManager = nullptr;
		std::unique_ptr<LabRuntimeLocatorBase> m_LabRuntimeLocator;
		RenderFrameBuilder* m_RenderFrameBuilder = nullptr;
		std::unique_ptr<ApplicationToolingIntegrationBase> m_ApplicationTooling;
		DebugDrawSystem* m_DebugDrawSystem = nullptr;

		LifecycleState m_LifecycleState = LifecycleState::Uninitialized;
		bool m_PlatformHostInitializationAttempted = false;
		bool m_ShutdownComplete = false;
		bool m_IsSuspended = false;
		int m_ExitCode = 0;
	};
}
