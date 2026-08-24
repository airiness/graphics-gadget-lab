#include "Graphics/Shader/ShaderManager.h"
#include "Core/Log/LogMacros.h"
#include "Compiler/ShaderCompiler.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "Graphics/Shader/Shader.h"
#include "Graphics/Shader/ShaderProgramCatalogPrivate.h"
#include "Targets/DX12ShaderTarget.h"
#include "Targets/Vulkan13ShaderTarget.h"

#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace gglab
{
	struct ShaderManager::BuildState
	{
		std::unique_ptr<ShaderCompiler> m_Compiler;
		ShaderDesc m_DefaultShaderConfig{};
	};

	struct ShaderManager::ShaderPreloadJob
	{
		struct Entry
		{
			ShaderProgramRef m_ProgramRef;
			ShaderResolvedRecipe m_Recipe;
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
		[[nodiscard]] bool IsHostDebuggerAttached() noexcept
		{
#if defined(_WIN32)
			return IsDebuggerPresent() != FALSE;
#else
			return false;
#endif
		}

		// Runtime-side adapter: the ShaderManager maps the active RHI backend to
		// a Shader target profile. RHIBackendType::Unknown has no profile and is
		// handled by the caller before this mapping runs.
		[[nodiscard]] constexpr ShaderTargetProfile GetShaderTargetProfile(
			RHIBackendType activeBackend) noexcept
		{
			return activeBackend == RHIBackendType::Vulkan
				? ShaderTargetProfile::GGLabVulkan13
				: ShaderTargetProfile::GGLabDX12;
		}

		[[nodiscard]] ShaderCompileTarget MakeShaderCompileTarget(
			ShaderTargetProfile profile, ShaderStage stage) noexcept
		{
			switch (profile)
			{
			case ShaderTargetProfile::GGLabDX12:
				return MakeDX12CompileTarget(stage);
			case ShaderTargetProfile::GGLabVulkan13:
				return MakeVulkan13CompileTarget(stage);
			}
			return {};
		}

		void ApplyActiveBackendTarget(
			ShaderDesc& desc, RHIBackendType activeBackend) noexcept
		{
			if (activeBackend == RHIBackendType::Unknown)
			{
				desc.m_Target.m_BinaryFormat = ShaderBinaryFormat::Unknown;
				desc.m_Target.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::None;
				desc.m_Target.m_BindingABIRevision = 0;
				desc.m_Target.m_CoordinateOptions = ShaderCoordinateOptions::None;
				return;
			}

			const ShaderCompileTarget backendTarget =
				MakeShaderCompileTarget(GetShaderTargetProfile(activeBackend), desc.m_Stage);
			desc.m_Target.m_BinaryFormat = backendTarget.m_BinaryFormat;
			desc.m_Target.m_SpirVTargetEnvironment = backendTarget.m_SpirVTargetEnvironment;
			desc.m_Target.m_BindingABIRevision = backendTarget.m_BindingABIRevision;
			desc.m_Target.m_CoordinateOptions = backendTarget.m_CoordinateOptions;
		}

		[[nodiscard]] ShaderRuntimeArtifact MakeRuntimeArtifact(
			ShaderArtifact artifact) noexcept
		{
			ShaderRuntimeArtifact runtimeArtifact{};
			runtimeArtifact.m_Manifest =
				BuildShaderRuntimeArtifactManifest(artifact.m_Manifest);
			runtimeArtifact.m_Binary = std::move(artifact.m_Binary);
			return runtimeArtifact;
		}
	}

	ShaderManager::ShaderManager(RHIBackendType activeBackend,
		std::filesystem::path shaderSourceRoot, std::filesystem::path shaderCacheRoot) noexcept :
		m_BuildState(std::make_unique<BuildState>()), m_ActiveBackend(activeBackend)
	{
		m_BuildState->m_Compiler = std::make_unique<ShaderCompiler>(
			std::move(shaderSourceRoot), std::move(shaderCacheRoot));

		m_BuildState->m_DefaultShaderConfig.m_Target.m_Flags |=
			IsHostDebuggerAttached() ? ShaderCompileFlags::Debug : ShaderCompileFlags::None;
		ApplyActiveBackendTarget(m_BuildState->m_DefaultShaderConfig, m_ActiveBackend);
		m_BuildState->m_DefaultShaderConfig.m_IncludeDirs = {
			m_BuildState->m_Compiler->GetSourceRootDirectory()
		};
		m_BuildState->m_DefaultShaderConfig.m_Defines = {};
		m_BuildState->m_Compiler->SetDefaultShaderConfig(
			m_BuildState->m_DefaultShaderConfig);
	}

	ShaderManager::~ShaderManager() = default;

	ShaderID ShaderManager::LoadProgram(const ShaderProgramRef& programRef) noexcept
	{
		if (!programRef.IsValid())
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

		std::optional<ShaderDesc> activeDesc =
			ResolveTransitionalShaderProgramBuild(programRef);
		if (!activeDesc)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"ShaderManager::LoadProgram: unknown program '{}::{}'.",
				programRef.m_ProgramId, programRef.m_VariantId);
			return {};
		}
		if (activeDesc->m_Stage != programRef.m_Stage)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"ShaderManager::LoadProgram: stage mismatch for program '{}::{}'.",
				programRef.m_ProgramId, programRef.m_VariantId);
			return {};
		}
		ApplyActiveBackendTarget(*activeDesc, m_ActiveBackend);
		const ShaderResolvedRecipe recipe = m_BuildState->m_Compiler->Resolve(*activeDesc);
		if (!recipe.IsSuccess())
		{
			GGLAB_LOG_GRAPHICS_ERROR("ShaderManager::LoadProgram: Resolve failed: {}",
				utils::ToString(recipe.m_Diagnostics.m_Message));
			return {};
		}

		if (!std::filesystem::exists(recipe.m_Request.m_SourcePath))
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"ShaderManager::LoadProgram: File not found: {}",
				recipe.m_Request.m_SourcePath.string());
			return {};
		}

		ShaderCompileResult compileResult =
			m_BuildState->m_Compiler->CompileOrLoad(recipe);
		if (!compileResult.IsSuccess() || !compileResult.m_Artifact.m_Binary.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"ShaderManager::LoadProgram: Compile failed: {}",
				utils::ToString(compileResult.m_Diagnostics.m_Message));
			return {};
		}
		ShaderRuntimeArtifact runtimeArtifact =
			MakeRuntimeArtifact(std::move(compileResult.m_Artifact));
		const ShaderArtifactRef artifactRef{
			.m_ArtifactId = runtimeArtifact.m_Manifest.m_ArtifactId,
		};
		const ShaderHash128 hash = ComputeShaderBinaryHash(
			runtimeArtifact.m_Binary, runtimeArtifact.m_Manifest.m_BinaryFormat);
		auto shader = std::make_unique<Shader>(programRef);
		shader->SetRuntimeArtifact(std::move(runtimeArtifact), artifactRef, hash, true);

		{
			std::unique_lock lock(m_Mutex);
			if (const auto iterator = m_ProgramIdMap.find(programRef);
				iterator != m_ProgramIdMap.end())
			{
				return iterator->second;
			}
			const ShaderProgramBindStatus bindStatus =
				m_ProgramRegistry.Bind(programRef, artifactRef);
			if (bindStatus == ShaderProgramBindStatus::InvalidProgram ||
				bindStatus == ShaderProgramBindStatus::InvalidArtifact ||
				bindStatus == ShaderProgramBindStatus::Failed)
			{
				return {};
			}

			ShaderID id{ static_cast<uint32_t>(m_Shaders.size()) };
			m_Shaders.push_back(std::move(shader));
			m_ProgramIdMap.emplace(programRef, id);
			return id;
		}
	}

	TaskHandle ShaderManager::PreloadAsync(
		TaskSystem& taskSystem, std::vector<ShaderProgramRef> programRefs,
		TaskPriority priority) noexcept
	{
		if (m_PreloadStatus == TaskStatus::Queued || m_PreloadStatus == TaskStatus::Running)
		{
			return m_PreloadTask;
		}

		if (programRefs.empty())
		{
			m_PreloadStatus = TaskStatus::Succeeded;
			m_PreloadError.clear();
			m_PreloadJob.reset();
			m_PreloadTask = {};
			return {};
		}

		ShaderDesc defaultConfig;
		RHIBackendType activeBackend = RHIBackendType::Unknown;
		const std::filesystem::path shaderSourceRoot =
			m_BuildState->m_Compiler->GetSourceRootDirectory();
		const std::filesystem::path shaderCacheRoot =
			m_BuildState->m_Compiler->GetCacheRootDirectory();
		{
			std::shared_lock lock(m_Mutex);
			defaultConfig = m_BuildState->m_DefaultShaderConfig;
			activeBackend = m_ActiveBackend;
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

		m_PreloadJob = job;
		m_PreloadStatus = TaskStatus::Queued;
		m_PreloadError.clear();
		m_PreloadTask = taskSystem.Submit(
			{
				.m_Name = "Shader.Preload",
				.m_Priority = priority,
			},
			[defaultConfig = std::move(defaultConfig), activeBackend, shaderSourceRoot,
				shaderCacheRoot, job](std::stop_token stopToken) noexcept
			{
				ShaderCompiler compiler(shaderSourceRoot, shaderCacheRoot);
				compiler.SetDefaultShaderConfig(defaultConfig);
				for (uint32_t index = 0; index < job->m_Programs.size(); ++index)
				{
					if (stopToken.stop_requested())
					{
						return TaskResult::Success();
					}

					job->m_CurrentIndex.store(index, std::memory_order_relaxed);
					ShaderPreloadJob::Entry entry{};
					entry.m_ProgramRef = job->m_Programs[index];
					std::optional<ShaderDesc> desc =
						ResolveTransitionalShaderProgramBuild(entry.m_ProgramRef);
					if (!desc)
					{
						return TaskResult::Failure(std::format(
							"Unknown shader program: {}::{}",
							entry.m_ProgramRef.m_ProgramId,
							entry.m_ProgramRef.m_VariantId));
					}
					if (desc->m_Stage != entry.m_ProgramRef.m_Stage)
					{
						return TaskResult::Failure(std::format(
							"Shader stage mismatch: {}::{}",
							entry.m_ProgramRef.m_ProgramId,
							entry.m_ProgramRef.m_VariantId));
					}
					ApplyActiveBackendTarget(*desc, activeBackend);
					entry.m_Recipe = compiler.Resolve(*desc);
					if (!entry.m_Recipe.IsSuccess())
					{
						return TaskResult::Failure(
							std::format("Shader resolve failed: {}",
								utils::ToString(entry.m_Recipe.m_Diagnostics.m_Message)));
					}
					if (!std::filesystem::exists(entry.m_Recipe.m_Request.m_SourcePath))
					{
						return TaskResult::Failure(
							std::format("Shader source file was not found: {}",
								entry.m_Recipe.m_Request.m_SourcePath.string()));
					}
					ShaderCompileResult result = compiler.CompileOrLoad(entry.m_Recipe);
					if (!result.IsSuccess() || !result.m_Artifact.m_Binary.IsValid())
					{
						return TaskResult::Failure(
							std::format("Shader compile produced no bytecode: {} ({})",
								entry.m_Recipe.m_Request.m_SourcePath.string(),
								utils::ToString(result.m_Diagnostics.m_Message)));
					}

					entry.m_Artifact = MakeRuntimeArtifact(std::move(result.m_Artifact));
					entry.m_ArtifactRef = {
						.m_ArtifactId = entry.m_Artifact.m_Manifest.m_ArtifactId,
					};
					entry.m_Hash = ComputeShaderBinaryHash(
						entry.m_Artifact.m_Binary,
						entry.m_Artifact.m_Manifest.m_BinaryFormat);
					job->m_Entries.push_back(std::move(entry));
					job->m_CompletedCount.store(index + 1, std::memory_order_relaxed);
				}
				return TaskResult::Success();
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
						"Async shader preload published {} shaders (queueMs={:.2f}, cpuMs={:.2f}).",
						job->m_Entries.size(), completion.m_QueueMilliseconds,
						completion.m_ExecutionMilliseconds);
				}
				m_PreloadTask = {};
			});
		if (!m_PreloadTask.IsValid())
		{
			m_PreloadStatus = TaskStatus::Failed;
			m_PreloadError = "TaskSystem rejected the shader preload task.";
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
			const ShaderProgramBindStatus bindStatus =
				m_ProgramRegistry.Bind(entry.m_ProgramRef, entry.m_ArtifactRef);
			if (bindStatus == ShaderProgramBindStatus::InvalidProgram ||
				bindStatus == ShaderProgramBindStatus::InvalidArtifact ||
				bindStatus == ShaderProgramBindStatus::Failed)
			{
				return false;
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

	int32_t ShaderManager::RefreshChanged() noexcept
	{
		std::unique_lock lock(m_Mutex);
		int32_t count = 0;
		for (auto& shader : m_Shaders)
		{
			if (shader)
			{
				if (RefreshShaderInternal(*shader))
				{
					++count;
				}
				else
				{
					const ShaderProgramRef& program = shader->GetProgramRef();
					GGLAB_LOG_GRAPHICS_ERROR("RefreshChanged: recompile failed: {}",
						std::format("{}::{}", program.m_ProgramId, program.m_VariantId));
				}
			}
		}
		if (count > 0)
		{
			m_Revision.fetch_add(1, std::memory_order_relaxed);
		}
		return count;
	}

	bool ShaderManager::RefreshShader(ShaderID shaderId) noexcept
	{
		std::unique_lock lock(m_Mutex);
		if (!shaderId.IsValid() || shaderId.Value() >= m_Shaders.size() ||
			!m_Shaders[shaderId.Value()])
		{
			return false;
		}
		const bool changed = RefreshShaderInternal(*m_Shaders[shaderId.Value()]);
		if (changed)
		{
			m_Revision.fetch_add(1, std::memory_order_relaxed);
		}
		return changed;
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

	bool ShaderManager::RefreshShaderInternal(Shader& shader) noexcept
	{
		std::optional<ShaderDesc> activeDesc =
			ResolveTransitionalShaderProgramBuild(shader.GetProgramRef());
		if (!activeDesc)
		{
			return false;
		}
		if (activeDesc->m_Stage != shader.GetProgramRef().m_Stage)
		{
			return false;
		}
		ApplyActiveBackendTarget(*activeDesc, m_ActiveBackend);
		const ShaderResolvedRecipe recipe = m_BuildState->m_Compiler->Resolve(*activeDesc);
		if (!recipe.IsSuccess())
		{
			GGLAB_LOG_GRAPHICS_ERROR("ShaderManager::RefreshShaderInternal: Resolve failed: {}",
				utils::ToString(recipe.m_Diagnostics.m_Message));
			return false;
		}
		ShaderCompileResult result = m_BuildState->m_Compiler->CompileOrLoad(recipe);
		if (!result.IsSuccess() || !result.m_Artifact.m_Binary.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR("ShaderManager::RefreshShaderInternal: Compile failed: {}",
				utils::ToString(result.m_Diagnostics.m_Message));
			return false;
		}

		ShaderRuntimeArtifact runtimeArtifact =
			MakeRuntimeArtifact(std::move(result.m_Artifact));
		const ShaderArtifactRef artifactRef{
			.m_ArtifactId = runtimeArtifact.m_Manifest.m_ArtifactId,
		};
		const ShaderHash128 hash = ComputeShaderBinaryHash(
			runtimeArtifact.m_Binary, runtimeArtifact.m_Manifest.m_BinaryFormat);
		const bool changed = shader.GetGeneration() == 0 ||
			artifactRef != shader.GetArtifactRef();
		const ShaderProgramBindStatus bindStatus =
			m_ProgramRegistry.Bind(shader.GetProgramRef(), artifactRef);
		if (bindStatus == ShaderProgramBindStatus::InvalidProgram ||
			bindStatus == ShaderProgramBindStatus::InvalidArtifact ||
			bindStatus == ShaderProgramBindStatus::Failed)
		{
			return false;
		}
		shader.SetRuntimeArtifact(std::move(runtimeArtifact), artifactRef, hash, changed);
		return changed;
	}

	std::optional<ShaderArtifactRef> ShaderManager::ResolveArtifact(
		const ShaderProgramRef& programRef) const noexcept
	{
		return m_ProgramRegistry.Resolve(programRef);
	}
}
