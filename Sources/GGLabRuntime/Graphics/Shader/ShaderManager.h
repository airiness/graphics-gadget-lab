#pragma once
#include "GGLabFoundation/Task/TaskTypes.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHITypes.h"
#include "Graphics/Shader/ShaderTypes.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace gglab
{
	class Shader;
	class TaskSystem;

	struct ShaderPreloadStatus
	{
		TaskStatus m_Status = TaskStatus::Invalid;
		uint32_t m_CompletedCount = 0;
		uint32_t m_TotalCount = 0;
		std::string m_CurrentShader;
		std::string m_Error;

		[[nodiscard]] bool IsPreparing() const noexcept
		{
			return m_Status == TaskStatus::Queued || m_Status == TaskStatus::Running;
		}
		[[nodiscard]] bool IsReady() const noexcept { return m_Status == TaskStatus::Succeeded; }
		[[nodiscard]] bool HasFailed() const noexcept
		{
			return m_Status == TaskStatus::Failed || m_Status == TaskStatus::Cancelled;
		}
	};

	class ShaderManager
	{
	public:
		ShaderManager(RHIBackendType activeBackend, std::filesystem::path shaderSourceRoot,
			std::filesystem::path shaderCacheRoot) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ShaderManager);
		~ShaderManager();

		RHIBackendType GetActiveBackend() const noexcept { return m_ActiveBackend; }

		ShaderID LoadProgram(const ShaderProgramRef& programRef) noexcept;
		[[nodiscard]] TaskHandle PreloadAsync(TaskSystem& taskSystem,
			std::vector<ShaderProgramRef> programRefs,
			TaskPriority priority = TaskPriority::High) noexcept;
		[[nodiscard]] ShaderPreloadStatus GetPreloadStatus() const;
		[[nodiscard]] std::optional<ShaderArtifactRef> ResolveArtifact(
			const ShaderProgramRef& programRef) const noexcept;

		int32_t RefreshChanged() noexcept;
		bool RefreshShader(ShaderID shaderId) noexcept;
		ShaderBytecode GetBytecode(ShaderID shaderId) const noexcept;
		ShaderHash128 GetHash(ShaderID shaderId) const noexcept;
		std::string GetDebugName(ShaderID shaderId) const noexcept;
		uint64_t GetGeneration(ShaderID shaderId) const noexcept;
		uint64_t GetRevision() const noexcept { return m_Revision.load(std::memory_order_relaxed); }

	private:
		struct BuildState;
		struct ShaderPreloadJob;

		bool RefreshShaderInternal(Shader& shader) noexcept;
		bool PublishPreloadJob(ShaderPreloadJob& job) noexcept;

	private:
		mutable std::shared_mutex m_Mutex;
		std::unordered_map<ShaderProgramRef, ShaderID, ShaderProgramRefHash> m_ProgramIdMap;
		std::vector<std::unique_ptr<Shader>> m_Shaders;
		ShaderProgramRegistry m_ProgramRegistry;

		std::unique_ptr<BuildState> m_BuildState;
		RHIBackendType m_ActiveBackend = RHIBackendType::Unknown;
		std::shared_ptr<ShaderPreloadJob> m_PreloadJob;
		TaskHandle m_PreloadTask{};
		TaskStatus m_PreloadStatus = TaskStatus::Invalid;
		std::string m_PreloadError;
		std::atomic_uint64_t m_Revision = 1;
	};
}
