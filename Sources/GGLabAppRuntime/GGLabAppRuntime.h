#pragma once

#include "ApplicationContentRegistration.h"
#include "AppRuntimeConfig.h"
#include "AppRuntimeHostServices.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "RuntimePaths.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistryArtifact.h"

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
		ShaderManagerInitializationFailed,
	};

	enum class AppRuntimeTickResult : uint8_t
	{
		Continue,
		Suspended,
		Exit,
	};

	enum class AppHostEventType : uint8_t
	{
		None,
		Suspended,
		Resumed,
		Resized,
		ExitRequested,
	};

	struct AppHostEvent
	{
		AppHostEventType m_Type = AppHostEventType::None;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
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
		std::filesystem::path m_ShaderArtifactRoot;
		ShaderProgramRegistryArtifactRef m_ActiveShaderRegistry;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_RHIContextFactory != nullptr && m_Input != nullptr &&
				m_WindowWidth > 0 && m_WindowHeight > 0;
		}
	};

	struct AppRuntimeTickInfo
	{
		ApplicationToolingIntegrationBase* m_ApplicationTooling = nullptr;

		struct PreContentUpdateHook final
		{
			void* m_Context = nullptr;
			bool (*m_Invoke)(void*) noexcept = nullptr;

			[[nodiscard]] bool Run() const noexcept
			{
				return !m_Invoke || m_Invoke(m_Context);
			}
		};

		// Runs before active content update and, once startup shaders are ready,
		// after the requested Demo and its pending Lab have been created but before
		// pending preparation/commit. Hosts use this seam to apply validated external
		// state to the first content frame.
		PreContentUpdateHook m_PreContentUpdate{};
	};

	struct AppRuntimeShutdownInfo
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
		void HandleHostEvent(const AppHostEvent& event) noexcept;
		void Shutdown(AppRuntimeShutdownInfo shutdownInfo = {}) noexcept;

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
		[[nodiscard]] bool BeginInitialShaderPreload(
			const ApplicationContentSelection& contentSelection) noexcept;
		void Resize(uint32_t width, uint32_t height) noexcept;

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
		bool m_ResizePending = false;
		bool m_ShutdownComplete = false;
	};
}
