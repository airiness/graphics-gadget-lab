#pragma once
#include "GGLabFoundation/Task/TaskTypes.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHITypes.h"
#include "Graphics/Shader/ShaderPipelineSnapshot.h"
#include "Graphics/Shader/ShaderTypes.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistryArtifact.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace gglab
{
	class Shader;
	class TaskSystem;

	enum class ShaderManagerInitializeStatus : uint8_t
	{
		Ready,
		InvalidCreateInfo,
		RegistryNotFound,
		RegistryReadFailure,
		MalformedRegistry,
	};

	struct ShaderManagerCreateInfo final
	{
		RHIBackendType m_ActiveBackend = RHIBackendType::Unknown;
		std::filesystem::path m_ArtifactRoot;
		ShaderProgramRegistryArtifactRef m_ActiveRegistry;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_ActiveBackend != RHIBackendType::Unknown &&
				!m_ArtifactRoot.empty() && m_ArtifactRoot.is_absolute() &&
				m_ActiveRegistry.IsValid();
		}
	};

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

	enum class ShaderRegistryActivationStatus : uint8_t
	{
		Activated,
		AlreadyActive,
		Busy,
		InvalidRegistryRef,
		RegistryNotFound,
		RegistryReadFailure,
		MalformedRegistry,
		MissingProgramBinding,
		ArtifactLoadFailure,
		Failed,
	};

	struct ShaderRegistryActivationResult final
	{
		ShaderRegistryActivationStatus m_Status = ShaderRegistryActivationStatus::Failed;
		uint32_t m_ChangedShaderCount = 0;
		std::string m_Error{};

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status == ShaderRegistryActivationStatus::Activated ||
				m_Status == ShaderRegistryActivationStatus::AlreadyActive;
		}
	};

	class ShaderManager
	{
	public:
		explicit ShaderManager(ShaderManagerCreateInfo createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ShaderManager);
		~ShaderManager();

		[[nodiscard]] bool IsReady() const noexcept
		{
			return m_InitializeStatus == ShaderManagerInitializeStatus::Ready;
		}
		[[nodiscard]] ShaderManagerInitializeStatus GetInitializeStatus() const noexcept
		{
			return m_InitializeStatus;
		}
		[[nodiscard]] RHIBackendType GetActiveBackend() const noexcept
		{
			return m_ActiveBackend;
		}
		[[nodiscard]] ShaderProgramRegistryArtifactRef
			GetActiveRegistryRef() const noexcept;

		ShaderID LoadProgram(const ShaderProgramRef& programRef) noexcept;
		[[nodiscard]] TaskHandle PreloadAsync(TaskSystem& taskSystem,
			std::vector<ShaderProgramRef> programRefs,
			TaskPriority priority = TaskPriority::High) noexcept;
		[[nodiscard]] ShaderPreloadStatus GetPreloadStatus() const;
		[[nodiscard]] std::optional<ShaderArtifactRef> ResolveArtifact(
			const ShaderProgramRef& programRef) const noexcept;
		[[nodiscard]] bool CaptureArtifactRefs(
			std::span<const ShaderProgramRef> programRefs,
			std::span<ShaderArtifactRef> outArtifactRefs,
			ShaderProgramRegistryArtifactRef& outRegistryRef) const noexcept;
		[[nodiscard]] ShaderRegistryActivationResult ActivateRegistry(
			const ShaderProgramRegistryArtifactRef& registryRef) noexcept;

		ShaderBytecode GetBytecode(ShaderID shaderId) const noexcept;
		ShaderHash128 GetHash(ShaderID shaderId) const noexcept;
		std::string GetDebugName(ShaderID shaderId) const noexcept;
		uint64_t GetGeneration(ShaderID shaderId) const noexcept;
		void CapturePipelineSnapshots(std::span<const ShaderID> shaderIds,
			std::span<ShaderPipelineSnapshot> outSnapshots) const noexcept;

	private:
		struct RuntimeState;
		struct ShaderPreloadJob;

		bool PublishPreloadJob(ShaderPreloadJob& job) noexcept;

	private:
		mutable std::shared_mutex m_Mutex;
		std::unordered_map<ShaderProgramRef, ShaderID, ShaderProgramRefHash> m_ProgramIdMap;
		std::vector<std::unique_ptr<Shader>> m_Shaders;
		std::unique_ptr<RuntimeState> m_RuntimeState;
		RHIBackendType m_ActiveBackend = RHIBackendType::Unknown;
		ShaderProgramRegistryArtifactRef m_ActiveRegistryRef{};
		ShaderManagerInitializeStatus m_InitializeStatus =
			ShaderManagerInitializeStatus::InvalidCreateInfo;
		std::shared_ptr<ShaderPreloadJob> m_PreloadJob;
		TaskHandle m_PreloadTask{};
		TaskStatus m_PreloadStatus = TaskStatus::Invalid;
		std::string m_PreloadError;
	};
}
