#pragma once

#include "ApplicationContentRegistration.h"
#include "AppRuntimeConfig.h"
#include "AppRuntimeHostServices.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "RuntimePaths.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace gglab
{
	enum class AppRuntimeLifecycleState : uint8_t
	{
		Uninitialized,
		Initializing,
		Running,
		Suspended,
		ExitRequested,
		Failed,
		ShuttingDown,
		Stopped,
	};

	enum class AppRuntimeInitializeResult : uint8_t
	{
		Succeeded,
		AlreadyInitialized,
		InvalidState,
		InvalidConfig,
		InvalidRuntimePaths,
	};

	enum class AppRuntimeServiceInitializeResult : uint8_t
	{
		Succeeded,
		AlreadyInitialized,
		InvalidState,
		InvalidCreateInfo,
		InvalidContentRegistration,
		StartupContentUnavailable,
		RendererInitializationFailed,
	};

	enum class AppRuntimeTickResult : uint8_t
	{
		Continue,
		Suspended,
		Exit,
	};

	enum class AppHostEventType : uint8_t
	{
		Suspended,
		Resumed,
		ExitRequested,
	};

	struct GGLabAppRuntimeCreateInfo
	{
		AppRuntimeConfig m_Config{};
		RuntimePaths m_Paths{};
		AppRuntimeHostServices m_HostServices{};
	};

	class ApplicationInput;
	class ApplicationToolingIntegrationBase;
	class AssetManager;
	class DebugDrawSystem;
	class DemoManager;
	class EnvironmentAssetController;
	class RenderFrameBuilder;
	class Renderer;
	class RHIContextFactoryBase;
	class ShaderManager;
	class TaskSystem;
	class Time;
	struct LoadingProgress;

	struct AppRuntimeServiceCreateInfo
	{
		RHIContextFactoryBase* m_RHIContextFactory = nullptr;
		ApplicationInput* m_Input = nullptr;
		ApplicationContentRegistration m_ContentRegistration{};
		uint32_t m_WindowWidth = 0;
		uint32_t m_WindowHeight = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_RHIContextFactory != nullptr && m_Input != nullptr &&
				m_WindowWidth > 0 && m_WindowHeight > 0;
		}
	};

	struct AppRuntimeTickInfo
	{
		ApplicationToolingIntegrationBase* m_ApplicationTooling = nullptr;
	};

	class GGLabAppRuntime final
	{
	public:
		GGLabAppRuntime() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(GGLabAppRuntime);
		~GGLabAppRuntime() noexcept;

		[[nodiscard]] AppRuntimeInitializeResult Initialize(
			const GGLabAppRuntimeCreateInfo& createInfo) noexcept;
		[[nodiscard]] AppRuntimeServiceInitializeResult InitializeServices(
			AppRuntimeServiceCreateInfo createInfo) noexcept;
		[[nodiscard]] AppRuntimeTickResult Tick(AppRuntimeTickInfo tickInfo = {}) noexcept;
		void Resize(uint32_t width, uint32_t height) noexcept;
		void HandleHostEvent(AppHostEventType eventType) noexcept;
		void Shutdown() noexcept;

		[[nodiscard]] AppRuntimeLifecycleState GetLifecycleState() const noexcept
		{
			return m_LifecycleState;
		}
		[[nodiscard]] bool AreServicesInitialized() const noexcept
		{
			return m_ServicesInitialized;
		}
		[[nodiscard]] Renderer* GetRenderer() const noexcept { return m_Renderer.get(); }
		[[nodiscard]] AssetManager* GetAssetManager() const noexcept
		{
			return m_AssetManager.get();
		}
		[[nodiscard]] EnvironmentAssetController* GetEnvironmentAssetController()
			const noexcept
		{
			return m_EnvironmentAssetController.get();
		}
		[[nodiscard]] TaskSystem* GetTaskSystem() const noexcept { return m_TaskSystem.get(); }
		[[nodiscard]] ApplicationInput* GetInput() const noexcept { return m_Input; }
		[[nodiscard]] ShaderManager* GetShaderManager() const noexcept
		{
			return m_ShaderManager.get();
		}
		[[nodiscard]] DemoManager* GetDemoManager() const noexcept
		{
			return m_DemoManager.get();
		}
		[[nodiscard]] RenderFrameBuilder* GetRenderFrameBuilder() const noexcept
		{
			return m_RenderFrameBuilder.get();
		}
		[[nodiscard]] DebugDrawSystem* GetDebugDrawSystem() const noexcept
		{
			return m_DebugDrawSystem.get();
		}
		[[nodiscard]] Time* GetTime() const noexcept { return m_Time.get(); }
		[[nodiscard]] std::optional<uint32_t> GetLabHostDemoIndex() const noexcept
		{
			return m_LabHostDemoIndex;
		}
		[[nodiscard]] LoadingProgress GetStartupLoadingProgress() const noexcept;

	private:
		[[nodiscard]] AppRuntimeServiceInitializeResult FailServiceInitialization(
			AppRuntimeServiceInitializeResult result) noexcept;
		void BeginInitialShaderPreload() noexcept;

		AppRuntimeConfig m_Config{};
		RuntimePaths m_Paths{};
		AppRuntimeHostServices m_HostServices{};
		ApplicationContentRegistration m_ContentRegistration{};
		std::unique_ptr<Renderer> m_Renderer;
		std::unique_ptr<Time> m_Time;
		std::unique_ptr<TaskSystem> m_TaskSystem;
		std::unique_ptr<AssetManager> m_AssetManager;
		std::unique_ptr<EnvironmentAssetController> m_EnvironmentAssetController;
		std::unique_ptr<ShaderManager> m_ShaderManager;
		std::unique_ptr<DemoManager> m_DemoManager;
		std::unique_ptr<RenderFrameBuilder> m_RenderFrameBuilder;
		std::unique_ptr<DebugDrawSystem> m_DebugDrawSystem;
		ApplicationInput* m_Input = nullptr;
		std::optional<uint32_t> m_LabHostDemoIndex;
		uint32_t m_WindowWidth = 0;
		uint32_t m_WindowHeight = 0;
		AppRuntimeLifecycleState m_LifecycleState = AppRuntimeLifecycleState::Uninitialized;
		bool m_ServicesInitialized = false;
		bool m_ShutdownComplete = false;
	};
}
