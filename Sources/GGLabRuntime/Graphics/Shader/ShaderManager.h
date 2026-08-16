#pragma once
#include "Core/Hash/KeyHash.h"
#include "GGLabFoundation/Task/TaskTypes.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHITypes.h"
#include "Graphics/Shader/Shader.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace gglab
{
	class ShaderCompiler;
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

	struct ShaderKey
	{
		ShaderHash128 m_KeyHash;
		auto AsTuple() const noexcept { return m_KeyHash.AsTuple(); }
		constexpr bool operator==(const ShaderKey&) const noexcept = default;
	};
	using ShaderKeyHash = KeyHash<ShaderKey>;

	class ShaderManager
	{
	public:
		ShaderManager(RHIBackendType activeBackend, std::filesystem::path shaderSourceRoot,
			std::filesystem::path shaderCacheRoot) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ShaderManager);
		~ShaderManager();

		void SetDefaultShaderConfig(const ShaderDesc& defaultDesc) noexcept;
		RHIBackendType GetActiveBackend() const noexcept { return m_ActiveBackend; }

		ShaderID LoadShader(const ShaderDesc& desc) noexcept;
		[[nodiscard]] TaskHandle PreloadAsync(TaskSystem& taskSystem,
			std::vector<ShaderDesc> descList, TaskPriority priority = TaskPriority::High) noexcept;
		[[nodiscard]] ShaderPreloadStatus GetPreloadStatus() const;

		int32_t RefreshChanged() noexcept;
		bool RefreshShader(ShaderID shaderId) noexcept;
		ShaderBytecode GetBytecode(ShaderID shaderId) const noexcept;
		ShaderHash128 GetHash(ShaderID shaderId) const noexcept;
		std::string GetDebugName(ShaderID shaderId) const noexcept;
		uint64_t GetGeneration(ShaderID shaderId) const noexcept;
		uint64_t GetRevision() const noexcept { return m_Revision.load(std::memory_order_relaxed); }

	private:
		struct ShaderPreloadJob;

		bool RefreshShaderInternal(Shader& shader) noexcept;
		bool RefreshShaderInternal(Shader& shader, const ShaderResolvedRecipe& recipe) noexcept;
		bool PublishPreloadJob(ShaderPreloadJob& job) noexcept;
		static void ApplyActiveBackendTarget(
			ShaderDesc& desc, RHIBackendType activeBackend) noexcept;

	private:
		mutable std::shared_mutex m_Mutex;
		std::unordered_map<ShaderKey, ShaderID, ShaderKeyHash> m_KeyIdMap;
		std::vector<std::unique_ptr<Shader>> m_Shaders;

		std::unique_ptr<ShaderCompiler> m_Compiler;
		RHIBackendType m_ActiveBackend = RHIBackendType::Unknown;
		ShaderDesc m_DefaultShaderConfig{};
		std::shared_ptr<ShaderPreloadJob> m_PreloadJob;
		TaskHandle m_PreloadTask{};
		TaskStatus m_PreloadStatus = TaskStatus::Invalid;
		std::string m_PreloadError;
		std::atomic_uint64_t m_Revision = 1;
	};
}
