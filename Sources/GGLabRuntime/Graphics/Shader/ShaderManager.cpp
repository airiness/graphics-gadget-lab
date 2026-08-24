#include "Graphics/Shader/ShaderManager.h"
#include "Core/Log/LogMacros.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "Graphics/Shader/Shader.h"
#include "ShaderArtifactRuntime/ShaderArtifact.h"
#include "ShaderArtifactRuntime/ShaderArtifactStore.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"
#include "ShaderArtifactRuntime/VulkanShaderRuntimeABI.h"

#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace gglab
{
	struct ShaderManager::RuntimeState
	{
		explicit RuntimeState(std::filesystem::path artifactRoot) :
			m_ArtifactRoot(std::move(artifactRoot)),
			m_ArtifactReader(ShaderLooseArtifactLocator(m_ArtifactRoot)),
			m_ArtifactStore(m_ArtifactReader)
		{
		}

		std::filesystem::path m_ArtifactRoot;
		ShaderProgramRegistryArtifact m_Registry;
		ShaderLooseArtifactReader m_ArtifactReader;
		ShaderArtifactStore m_ArtifactStore;
	};

	struct ShaderManager::ShaderPreloadJob
	{
		struct Entry
		{
			ShaderProgramRef m_ProgramRef;
			ShaderRuntimeArtifact m_Artifact;
			ShaderArtifactRef m_ArtifactRef{};
			ShaderHash128 m_Hash{};
		};

		std::vector<ShaderProgramRef> m_Programs;
		std::vector<Entry> m_Entries;
		std::vector<std::string> m_Labels;
		std::atomic_uint32_t m_CompletedCount = 0;
		std::atomic_uint32_t m_CurrentIndex = std::numeric_limits<uint32_t>::max();
	};

	namespace
	{
		[[nodiscard]] constexpr ShaderTargetProfile GetActiveTargetProfile(
			RHIBackendType activeBackend) noexcept
		{
			return activeBackend == RHIBackendType::Vulkan
				? ShaderTargetProfile::GGLabVulkan13
				: ShaderTargetProfile::GGLabDX12;
		}

		[[nodiscard]] constexpr ShaderArtifactCompatibilityRequest
			MakeCompatibilityRequest(
				RHIBackendType activeBackend, ShaderStage stage) noexcept
		{
			if (activeBackend == RHIBackendType::Vulkan)
			{
				return {
					.m_TargetProfile = ShaderTargetProfile::GGLabVulkan13,
					.m_BinaryFormat = ShaderBinaryFormat::SpirV,
					.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::Vulkan1_3,
					.m_BindingABIRevision = GGLabVulkanShaderRuntimeABI.m_Revision,
					.m_CoordinateOptions = GetGGLabVulkanShaderCoordinateOptions(stage),
					.m_Stage = stage,
				};
			}
			return {
				.m_TargetProfile = ShaderTargetProfile::GGLabDX12,
				.m_BinaryFormat = ShaderBinaryFormat::Dxil,
				.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::None,
				.m_BindingABIRevision = 0,
				.m_CoordinateOptions = ShaderCoordinateOptions::None,
				.m_Stage = stage,
			};
		}

		[[nodiscard]] bool LoadProgramArtifact(
			const ShaderProgramRegistryArtifact& registry,
			ShaderArtifactStore& store,
			RHIBackendType activeBackend,
			const ShaderProgramRef& programRef,
			ShaderRuntimeArtifact& artifact,
			ShaderArtifactRef& artifactRef,
			ShaderHash128& hash,
			std::string& error) noexcept
		{
			const std::optional<ShaderArtifactRef> resolved =
				ResolveShaderProgramRegistryArtifact(
					registry, programRef, GetActiveTargetProfile(activeBackend));
			if (!resolved)
			{
				error = std::format("Program registry has no binding for {}::{} (stage={}).",
					programRef.m_ProgramId, programRef.m_VariantId,
					static_cast<uint32_t>(programRef.m_Stage));
				return false;
			}

			ShaderArtifactLoadResult load = store.LoadArtifact(
				*resolved, MakeCompatibilityRequest(activeBackend, programRef.m_Stage));
			if (!load.IsSuccess())
			{
				error = std::format(
					"Artifact load failed for {}::{} (loadStatus={}, compatibilityStatus={}).",
					programRef.m_ProgramId, programRef.m_VariantId,
					static_cast<uint32_t>(load.m_Status),
					static_cast<uint32_t>(load.m_Compatibility.m_Status));
				return false;
			}

			artifactRef = *resolved;
			hash = ComputeShaderBinaryHash(
				load.m_Artifact.m_Binary, load.m_Artifact.m_Manifest.m_BinaryFormat);
			artifact = std::move(load.m_Artifact);
			return true;
		}
	}

	ShaderManager::ShaderManager(ShaderManagerCreateInfo createInfo) noexcept :
		m_ActiveBackend(createInfo.m_ActiveBackend),
		m_ActiveRegistryRef(createInfo.m_ActiveRegistry)
	{
		if (!createInfo.IsValid())
		{
			return;
		}

		try
		{
			auto state = std::make_unique<RuntimeState>(std::move(createInfo.m_ArtifactRoot));
			ShaderLooseProgramRegistryArtifactReader registryReader{
				ShaderLooseProgramRegistryArtifactLocator(state->m_ArtifactRoot)
			};
			ShaderProgramRegistryArtifactReadResult registryRead =
				registryReader.ReadArtifact(m_ActiveRegistryRef);
			if (!registryRead.IsSuccess())
			{
				switch (registryRead.m_Status)
				{
				case ShaderProgramRegistryArtifactReadStatus::NotFound:
					m_InitializeStatus = ShaderManagerInitializeStatus::RegistryNotFound;
					break;
				case ShaderProgramRegistryArtifactReadStatus::MalformedArtifact:
					m_InitializeStatus = ShaderManagerInitializeStatus::MalformedRegistry;
					break;
				default:
					m_InitializeStatus = ShaderManagerInitializeStatus::RegistryReadFailure;
					break;
				}
				return;
			}

			state->m_Registry = std::move(registryRead.m_Artifact);
			m_RuntimeState = std::move(state);
			m_InitializeStatus = ShaderManagerInitializeStatus::Ready;
		}
		catch (...)
		{
			m_InitializeStatus = ShaderManagerInitializeStatus::RegistryReadFailure;
		}
	}

	ShaderManager::~ShaderManager() = default;

	ShaderID ShaderManager::LoadProgram(const ShaderProgramRef& programRef) noexcept
	{
		if (!IsReady() || !programRef.IsValid())
		{
			return {};
		}
		{
			std::shared_lock lock(m_Mutex);
			if (const auto iterator = m_ProgramIdMap.find(programRef);
				iterator != m_ProgramIdMap.end())
			{
				return iterator->second;
			}
		}

		ShaderRuntimeArtifact artifact;
		ShaderArtifactRef artifactRef{};
		ShaderHash128 hash{};
		std::string error;
		if (!LoadProgramArtifact(m_RuntimeState->m_Registry, m_RuntimeState->m_ArtifactStore,
			m_ActiveBackend, programRef, artifact, artifactRef, hash, error))
		{
			GGLAB_LOG_GRAPHICS_ERROR("ShaderManager::LoadProgram: {}", error);
			return {};
		}

		auto shader = std::make_unique<Shader>(programRef);
		shader->SetRuntimeArtifact(std::move(artifact), artifactRef, hash, true);
		std::unique_lock lock(m_Mutex);
		if (const auto iterator = m_ProgramIdMap.find(programRef);
			iterator != m_ProgramIdMap.end())
		{
			return iterator->second;
		}
		const ShaderID id{ static_cast<uint32_t>(m_Shaders.size()) };
		m_Shaders.push_back(std::move(shader));
		m_ProgramIdMap.emplace(programRef, id);
		return id;
	}

	TaskHandle ShaderManager::PreloadAsync(
		TaskSystem& taskSystem, std::vector<ShaderProgramRef> programRefs,
		TaskPriority priority) noexcept
	{
		if (m_PreloadStatus == TaskStatus::Queued || m_PreloadStatus == TaskStatus::Running)
		{
			return m_PreloadTask;
		}
		if (!IsReady())
		{
			m_PreloadStatus = TaskStatus::Failed;
			m_PreloadError = "ShaderManager has no valid active Program Registry Artifact.";
			return {};
		}
		if (programRefs.empty())
		{
			m_PreloadStatus = TaskStatus::Succeeded;
			m_PreloadError.clear();
			m_PreloadJob.reset();
			m_PreloadTask = {};
			return {};
		}

		auto job = std::make_shared<ShaderPreloadJob>();
		job->m_Programs = std::move(programRefs);
		job->m_Entries.reserve(job->m_Programs.size());
		job->m_Labels.reserve(job->m_Programs.size());
		for (const ShaderProgramRef& program : job->m_Programs)
		{
			job->m_Labels.push_back(
				std::format("{}::{}", program.m_ProgramId, program.m_VariantId));
		}

		const std::filesystem::path artifactRoot = m_RuntimeState->m_ArtifactRoot;
		const ShaderProgramRegistryArtifact registry = m_RuntimeState->m_Registry;
		const RHIBackendType activeBackend = m_ActiveBackend;
		m_PreloadJob = job;
		m_PreloadStatus = TaskStatus::Queued;
		m_PreloadError.clear();
		m_PreloadTask = taskSystem.Submit(
			{
				.m_Name = "Shader.Preload",
				.m_Priority = priority,
			},
			[artifactRoot, registry, activeBackend, job](std::stop_token stopToken) noexcept
			{
				try
				{
					ShaderLooseArtifactReader reader{ ShaderLooseArtifactLocator(artifactRoot) };
					ShaderArtifactStore store(reader);
					for (uint32_t index = 0; index < job->m_Programs.size(); ++index)
					{
						if (stopToken.stop_requested())
						{
							return TaskResult::Success();
						}

						job->m_CurrentIndex.store(index, std::memory_order_relaxed);
						ShaderPreloadJob::Entry entry{};
						entry.m_ProgramRef = job->m_Programs[index];
						std::string error;
						if (!LoadProgramArtifact(registry, store, activeBackend,
							entry.m_ProgramRef, entry.m_Artifact, entry.m_ArtifactRef,
							entry.m_Hash, error))
						{
							return TaskResult::Failure(std::move(error));
						}
						job->m_Entries.push_back(std::move(entry));
						job->m_CompletedCount.store(index + 1, std::memory_order_relaxed);
					}
					return TaskResult::Success();
				}
				catch (...)
				{
					return TaskResult::Failure("Shader artifact preload failed unexpectedly.");
				}
			},
			[this, job](const TaskCompletionInfo& completion) noexcept
			{
				m_PreloadStatus = completion.m_Status;
				m_PreloadError = completion.m_Error;
				if (completion.m_Status == TaskStatus::Succeeded && !PublishPreloadJob(*job))
				{
					m_PreloadStatus = TaskStatus::Failed;
					m_PreloadError = "Failed to publish preloaded shaders.";
				}
				if (m_PreloadStatus == TaskStatus::Succeeded)
				{
					GGLAB_LOG_GRAPHICS_INFO(
						"Async artifact preload published {} shaders (queueMs={:.2f}, cpuMs={:.2f}).",
						job->m_Entries.size(), completion.m_QueueMilliseconds,
						completion.m_ExecutionMilliseconds);
				}
				m_PreloadTask = {};
			});
		if (!m_PreloadTask.IsValid())
		{
			m_PreloadStatus = TaskStatus::Failed;
			m_PreloadError = "TaskSystem rejected the shader artifact preload task.";
		}
		return m_PreloadTask;
	}

	ShaderPreloadStatus ShaderManager::GetPreloadStatus() const
	{
		ShaderPreloadStatus result{};
		result.m_Status = m_PreloadStatus;
		result.m_Error = m_PreloadError;
		const auto job = m_PreloadJob;
		if (!job)
		{
			return result;
		}
		result.m_TotalCount = static_cast<uint32_t>(job->m_Programs.size());
		result.m_CompletedCount = job->m_CompletedCount.load(std::memory_order_relaxed);
		const uint32_t currentIndex = job->m_CurrentIndex.load(std::memory_order_relaxed);
		if (currentIndex < job->m_Labels.size())
		{
			result.m_CurrentShader = job->m_Labels[currentIndex];
		}
		return result;
	}

	bool ShaderManager::PublishPreloadJob(ShaderPreloadJob& job) noexcept
	{
		if (job.m_Entries.size() != job.m_Programs.size())
		{
			return false;
		}

		std::unique_lock lock(m_Mutex);
		for (auto& entry : job.m_Entries)
		{
			if (m_ProgramIdMap.contains(entry.m_ProgramRef))
			{
				continue;
			}
			auto shader = std::make_unique<Shader>(entry.m_ProgramRef);
			shader->SetRuntimeArtifact(
				std::move(entry.m_Artifact), entry.m_ArtifactRef, entry.m_Hash, true);
			const ShaderID id{ static_cast<uint32_t>(m_Shaders.size()) };
			m_Shaders.push_back(std::move(shader));
			m_ProgramIdMap.emplace(entry.m_ProgramRef, id);
		}
		return true;
	}

	ShaderBytecode ShaderManager::GetBytecode(ShaderID shaderId) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		if (shaderId.IsValid() && shaderId.Value() < m_Shaders.size() &&
			m_Shaders[shaderId.Value()])
		{
			return m_Shaders[shaderId.Value()]->GetBytecode();
		}
		GGLAB_LOG_GRAPHICS_ERROR(
			"ShaderManager::GetBytecode: Invalid shader ID {}", shaderId.Value());
		return {};
	}

	ShaderHash128 ShaderManager::GetHash(ShaderID shaderId) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		if (shaderId.IsValid() && shaderId.Value() < m_Shaders.size() &&
			m_Shaders[shaderId.Value()])
		{
			return m_Shaders[shaderId.Value()]->GetHash();
		}
		return {};
	}

	std::string ShaderManager::GetDebugName(ShaderID shaderId) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		if (!shaderId.IsValid() || shaderId.Value() >= m_Shaders.size() ||
			!m_Shaders[shaderId.Value()])
		{
			return {};
		}
		const ShaderProgramRef& program = m_Shaders[shaderId.Value()]->GetProgramRef();
		return std::format("{}::{}", program.m_ProgramId, program.m_VariantId);
	}

	uint64_t ShaderManager::GetGeneration(ShaderID shaderId) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		if (shaderId.IsValid() && shaderId.Value() < m_Shaders.size() &&
			m_Shaders[shaderId.Value()])
		{
			return m_Shaders[shaderId.Value()]->GetGeneration();
		}
		return 0;
	}

	std::optional<ShaderArtifactRef> ShaderManager::ResolveArtifact(
		const ShaderProgramRef& programRef) const noexcept
	{
		if (!IsReady())
		{
			return std::nullopt;
		}
		return ResolveShaderProgramRegistryArtifact(m_RuntimeState->m_Registry,
			programRef, GetActiveTargetProfile(m_ActiveBackend));
	}
}
