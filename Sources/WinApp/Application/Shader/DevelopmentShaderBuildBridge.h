#pragma once
#include "GGLabFoundation/Task/TaskTypes.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHITypes.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistryArtifact.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

namespace gglab
{
	class Renderer;
	class ShaderManager;
	class TaskSystem;

	enum class DevelopmentShaderBuildStatus : uint8_t
	{
		Succeeded,
		InvalidInput,
		ToolNotFound,
		ToolIncompatible,
		ProcessLaunchFailed,
		ProcessFailed,
		TimedOut,
		Cancelled,
		ActiveRegistryUnavailable,
		Failed,
	};

	struct DevelopmentShaderBuildRequest final
	{
		RHIBackendType m_ActiveBackend = RHIBackendType::Unknown;
		std::filesystem::path m_ShaderCompilerPath{};
		std::filesystem::path m_ShaderSourceRoot{};
		std::filesystem::path m_ShaderCacheRoot{};
		std::filesystem::path m_ArtifactRoot{};

		[[nodiscard]] bool IsValid() const noexcept;
	};

	struct DevelopmentShaderBuildResult final
	{
		DevelopmentShaderBuildStatus m_Status = DevelopmentShaderBuildStatus::Failed;
		ShaderProgramRegistryArtifactRef m_RegistryRef{};
		std::string m_Diagnostics{};

		[[nodiscard]] bool IsSuccess() const noexcept
		{
			return m_Status == DevelopmentShaderBuildStatus::Succeeded &&
				m_RegistryRef.IsValid();
		}
	};

	[[nodiscard]] DevelopmentShaderBuildResult RunDevelopmentShaderBuild(
		const DevelopmentShaderBuildRequest& request,
		std::stop_token stopToken = {}) noexcept;

	class DevelopmentShaderHotReloadSystem final
	{
	public:
		struct CreateInfo final
		{
			DevelopmentShaderBuildRequest m_BuildRequest{};
			TaskSystem* m_TaskSystem = nullptr;
			ShaderManager* m_ShaderManager = nullptr;
			Renderer* m_Renderer = nullptr;
		};

		explicit DevelopmentShaderHotReloadSystem(CreateInfo createInfo) noexcept;
		DevelopmentShaderHotReloadSystem(
			const DevelopmentShaderHotReloadSystem&) = delete;
		DevelopmentShaderHotReloadSystem& operator=(
			const DevelopmentShaderHotReloadSystem&) = delete;
		~DevelopmentShaderHotReloadSystem();

		[[nodiscard]] bool Initialize() noexcept;
		void Update() noexcept;
		void Shutdown() noexcept;

	private:
		struct BuildJob;

		[[nodiscard]] std::optional<uint64_t> ComputeSourceFingerprint() const noexcept;
		void StartBuild() noexcept;
		void TryActivatePendingRegistry() noexcept;

		DevelopmentShaderBuildRequest m_BuildRequest{};
		TaskSystem* m_TaskSystem = nullptr;
		ShaderManager* m_ShaderManager = nullptr;
		Renderer* m_Renderer = nullptr;
		ShaderID m_TemporalAAResolveShader{};
		std::shared_ptr<BuildJob> m_BuildJob{};
		TaskHandle m_BuildTask{};
		std::optional<ShaderProgramRegistryArtifactRef> m_PendingRegistry{};
		uint64_t m_ObservedSourceFingerprint = 0;
		std::chrono::steady_clock::time_point m_NextScan{};
		std::chrono::steady_clock::time_point m_LastObservedChange{};
		bool m_RebuildRequested = false;
		bool m_Initialized = false;
		bool m_ShuttingDown = false;
	};
}
