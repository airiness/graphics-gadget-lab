#include "Graphics/Shader/ShaderManager.h"
#include "Core/Log/LogMacros.h"
#include "Core/Task/TaskSystem.h"
#include "Core/Platform/Win/Win32StringUtils.h"
#include "Core/Utility/StringUtils.h"
#include "Graphics/Shader/ShaderCompiler.h"

#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace gglab
{
	struct ShaderManager::ShaderPreloadJob
	{
		struct Entry
		{
			ShaderDesc m_Desc;
			ShaderDesc m_NormalizedDesc;
			ShaderCompileArtifact m_Artifact;
		};

		std::vector<ShaderDesc> m_Descs;
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

		[[nodiscard]] constexpr std::string_view ShaderStageAbbreviation(ShaderStage stage) noexcept
		{
			switch (stage)
			{
			case ShaderStage::Vertex:
				return "VS";
			case ShaderStage::Pixel:
				return "PS";
			case ShaderStage::Hull:
				return "HS";
			case ShaderStage::Domain:
				return "DS";
			case ShaderStage::Geometry:
				return "GS";
			case ShaderStage::Mesh:
				return "MS";
			case ShaderStage::Compute:
				return "CS";
			}
			return "Unknown";
		}
	}

	ShaderManager::ShaderManager(RHIBackendType activeBackend,
		std::filesystem::path shaderSourceRoot, std::filesystem::path shaderCacheRoot) noexcept :
		m_ActiveBackend(activeBackend)
	{
		m_Compiler = std::make_unique<ShaderCompiler>(
			std::move(shaderSourceRoot), std::move(shaderCacheRoot));

		m_DefaultShaderConfig.m_Target.m_Flags |=
			IsHostDebuggerAttached() ? ShaderCompileFlags::Debug : ShaderCompileFlags::None;
		ApplyActiveBackendTarget(m_DefaultShaderConfig, m_ActiveBackend);
		m_DefaultShaderConfig.m_IncludeDirs = { m_Compiler->GetSourceRootDirectory() };
		m_DefaultShaderConfig.m_Defines = {};
		m_Compiler->SetDefaultShaderConfig(m_DefaultShaderConfig);
	}

	ShaderManager::~ShaderManager() = default;

	void ShaderManager::SetDefaultShaderConfig(const ShaderDesc& defaultDesc) noexcept
	{
		std::unique_lock lock(m_Mutex);
		m_DefaultShaderConfig = defaultDesc;
		ApplyActiveBackendTarget(m_DefaultShaderConfig, m_ActiveBackend);
		m_Compiler->SetDefaultShaderConfig(m_DefaultShaderConfig);
	}

	ShaderID ShaderManager::LoadShader(const ShaderDesc& desc) noexcept
	{
		ShaderDesc norm = NormalizeForActiveBackend(desc);

		const auto keyHash = ShaderCompiler::ComputeRecipeHash(norm);
		ShaderKey key{ .m_KeyHash = keyHash };

		// return if exist.
		{
			std::shared_lock lock(m_Mutex);
			if (auto it = m_KeyIdMap.find(key); it != m_KeyIdMap.end())
			{
				return it->second;
			}
		}

		// create shader if not exist
		if (!std::filesystem::exists(norm.m_SourcePath))
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"ShaderManager::LoadShader: File not found: {}", norm.m_SourcePath.string());
			return ShaderID();
		}

		std::unique_ptr<Shader> shader = std::make_unique<Shader>(desc);
		if (!RefreshShaderInternal(*shader, norm))
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"ShaderManager::LoadShader: Shader compile failed: {}", norm.m_SourcePath.string());
			return ShaderID();
		}

		{
			std::unique_lock lock(m_Mutex);
			if (auto it = m_KeyIdMap.find(key); it != m_KeyIdMap.end())
			{
				return it->second;
			}

			ShaderID id{ static_cast<uint32_t>(m_Shaders.size()) };
			m_Shaders.push_back(std::move(shader));
			m_KeyIdMap.emplace(key, id);
			return id;
		}
	}

	TaskHandle ShaderManager::PreloadAsync(
		TaskSystem& taskSystem, std::vector<ShaderDesc> descList, TaskPriority priority) noexcept
	{
		if (m_PreloadStatus == TaskStatus::Queued || m_PreloadStatus == TaskStatus::Running)
		{
			return m_PreloadTask;
		}

		if (descList.empty())
		{
			m_PreloadStatus = TaskStatus::Succeeded;
			m_PreloadError.clear();
			m_PreloadJob.reset();
			m_PreloadTask = {};
			return {};
		}

		ShaderDesc defaultConfig;
		RHIBackendType activeBackend = RHIBackendType::Unknown;
		const std::filesystem::path shaderSourceRoot = m_Compiler->GetSourceRootDirectory();
		const std::filesystem::path shaderCacheRoot = m_Compiler->GetCacheRootDirectory();
		{
			std::shared_lock lock(m_Mutex);
			defaultConfig = m_DefaultShaderConfig;
			activeBackend = m_ActiveBackend;
		}

		auto job = std::make_shared<ShaderPreloadJob>();
		job->m_Descs = std::move(descList);
		job->m_Entries.reserve(job->m_Descs.size());
		job->m_Labels.reserve(job->m_Descs.size());
		for (const ShaderDesc& desc : job->m_Descs)
		{
			job->m_Labels.push_back(
				std::format("{} [{}]", desc.m_SourcePath.filename().generic_string(),
					ShaderStageAbbreviation(desc.m_Stage)));
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
				for (uint32_t index = 0; index < job->m_Descs.size(); ++index)
				{
					if (stopToken.stop_requested())
					{
						return TaskResult::Success();
					}

					job->m_CurrentIndex.store(index, std::memory_order_relaxed);
					ShaderPreloadJob::Entry entry{};
					entry.m_Desc = job->m_Descs[index];
					ApplyActiveBackendTarget(entry.m_Desc, activeBackend);
					entry.m_NormalizedDesc = compiler.NormalizeShaderDesc(entry.m_Desc);
					if (!std::filesystem::exists(entry.m_NormalizedDesc.m_SourcePath))
					{
						return TaskResult::Failure(
							std::format("Shader source file was not found: {}",
								entry.m_NormalizedDesc.m_SourcePath.string()));
					}
					entry.m_Artifact = compiler.CompileOrLoadArtifact(entry.m_NormalizedDesc);
					if (!entry.m_Artifact.m_Binary.IsValid())
					{
						return TaskResult::Failure(
							std::format("Shader compile produced no bytecode: {}",
								entry.m_NormalizedDesc.m_SourcePath.string()));
					}

					std::error_code errorCode;
					entry.m_Artifact.m_SourceTimeStamp = std::filesystem::last_write_time(
						entry.m_NormalizedDesc.m_SourcePath, errorCode);
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

		result.m_TotalCount = static_cast<uint32_t>(job->m_Descs.size());
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
		if (job.m_Entries.size() != job.m_Descs.size())
		{
			return false;
		}

		std::unique_lock lock(m_Mutex);
		for (auto& entry : job.m_Entries)
		{
			const ShaderKey key{
				.m_KeyHash = ShaderCompiler::ComputeRecipeHash(entry.m_NormalizedDesc),
			};
			if (m_KeyIdMap.contains(key))
			{
				continue;
			}

			auto shader = std::make_unique<Shader>(entry.m_Desc);
			shader->SetCompileArtifact(std::move(entry.m_Artifact), true);
			const ShaderID id{ static_cast<uint32_t>(m_Shaders.size()) };
			m_Shaders.push_back(std::move(shader));
			m_KeyIdMap.emplace(key, id);
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
					GGLAB_LOG_GRAPHICS_ERROR("RefreshChanged: recompile failed: {}",
						shader->GetDesc().m_SourcePath.string());
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
			return m_Shaders[shaderId.Value()]->GetCompileArtifact().m_Hash;
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

		const ShaderDesc& desc = m_Shaders[shaderId.Value()]->GetDesc();
		const std::string source = desc.m_SourcePath.filename().string();
		const std::string entry = utils::ToString(desc.m_Entry);
		return entry.empty() ? source : std::format("{}::{}", source, entry);
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
		ShaderDesc norm = NormalizeForActiveBackend(shader.GetDesc());
		return RefreshShaderInternal(shader, norm);
	}

	bool ShaderManager::RefreshShaderInternal(
		Shader& shader, const ShaderDesc& normalizedDesc) noexcept
	{
		ShaderCompileArtifact artifact = m_Compiler->CompileOrLoadArtifact(normalizedDesc);
		if (!artifact.m_Binary.IsValid())
		{
			return false;
		}

		const auto changed = (shader.GetGeneration() == 0) ||
			(artifact.m_Hash != shader.GetCompileArtifact().m_Hash);

		std::error_code errorCode;
		artifact.m_SourceTimeStamp =
			std::filesystem::exists(normalizedDesc.m_SourcePath, errorCode)
			? std::filesystem::last_write_time(normalizedDesc.m_SourcePath, errorCode)
			: std::filesystem::file_time_type{};

		shader.SetCompileArtifact(std::move(artifact), changed);
		return changed;
	}

	ShaderDesc ShaderManager::NormalizeForActiveBackend(const ShaderDesc& desc) const noexcept
	{
		ShaderDesc activeDesc = desc;
		ApplyActiveBackendTarget(activeDesc, m_ActiveBackend);
		return m_Compiler->NormalizeShaderDesc(activeDesc);
	}

	void ShaderManager::ApplyActiveBackendTarget(
		ShaderDesc& desc, RHIBackendType activeBackend) noexcept
	{
		switch (activeBackend)
		{
		case RHIBackendType::DX12:
			desc.m_Target.m_BinaryFormat = ShaderBinaryFormat::Dxil;
			desc.m_Target.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::None;
			desc.m_Target.m_BindingABIRevision = 0;
			desc.m_Target.m_CoordinateOptions = ShaderCoordinateOptions::None;
			break;
		case RHIBackendType::Vulkan:
		{
			const ShaderCompileTarget target =
				ShaderCompiler::MakeVulkanSpirVTarget(desc.m_Stage);
			desc.m_Target.m_BinaryFormat = target.m_BinaryFormat;
			desc.m_Target.m_SpirVTargetEnvironment = target.m_SpirVTargetEnvironment;
			desc.m_Target.m_BindingABIRevision = target.m_BindingABIRevision;
			desc.m_Target.m_CoordinateOptions = target.m_CoordinateOptions;
			break;
		}
		case RHIBackendType::Unknown:
			desc.m_Target.m_BinaryFormat = ShaderBinaryFormat::Unknown;
			desc.m_Target.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::None;
			desc.m_Target.m_BindingABIRevision = 0;
			desc.m_Target.m_CoordinateOptions = ShaderCoordinateOptions::None;
			break;
		}
		desc.m_Target.m_DxcVersion.clear();
	}
}
