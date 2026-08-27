#include "Application/Shader/DevelopmentShaderBuildBridge.h"
#include "Application/Shader/DevelopmentShaderBuildProcessClient.h"
#include "AppRuntimeLog.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace gglab
{
	struct DevelopmentShaderHotReloadSystem::BuildJob
	{
		DevelopmentShaderBuildResult m_Result{};
	};

	namespace
	{
		constexpr std::chrono::milliseconds SourceScanInterval{ 250 };
		constexpr std::chrono::milliseconds SourceChangeDebounce{ 200 };
		[[nodiscard]] bool IsShaderSourcePath(const std::filesystem::path& path) noexcept
		{
			const std::wstring extension = path.extension().wstring();
			return extension == L".hlsl" || extension == L".hlsli";
		}

		void HashBytes(uint64_t& hash, const void* data, size_t size) noexcept
		{
			constexpr uint64_t FnvPrime = 1099511628211ull;
			const auto* bytes = static_cast<const unsigned char*>(data);
			for (size_t index = 0; index < size; ++index)
			{
				hash ^= bytes[index];
				hash *= FnvPrime;
			}
		}

	}

	bool DevelopmentShaderBuildRequest::IsValid() const noexcept
	{
		const bool backendSupported = m_ActiveBackend == RHIBackendType::DX12 ||
			m_ActiveBackend == RHIBackendType::Vulkan;
		return backendSupported &&
			!m_ShaderCompilerPath.empty() && m_ShaderCompilerPath.is_absolute() &&
			!m_ShaderSourceRoot.empty() && m_ShaderSourceRoot.is_absolute() &&
			!m_ShaderCacheRoot.empty() && m_ShaderCacheRoot.is_absolute() &&
			!m_ArtifactRoot.empty() && m_ArtifactRoot.is_absolute();
	}

	DevelopmentShaderBuildResult RunDevelopmentShaderBuild(
		const DevelopmentShaderBuildRequest& request, std::stop_token stopToken) noexcept
	{
		if (!request.IsValid())
		{
			return {
				.m_Status = DevelopmentShaderBuildStatus::InvalidInput,
				.m_Diagnostics = "External shader build requires a known backend and absolute paths.",
			};
		}

		std::error_code errorCode;
		if (!std::filesystem::is_regular_file(request.m_ShaderCompilerPath, errorCode))
		{
			return {
				.m_Status = DevelopmentShaderBuildStatus::ToolNotFound,
				.m_Diagnostics = "gglab-shaderc.exe was not found beside the host executable.",
			};
		}

		DevelopmentShaderBuildResult result =
			RunDevelopmentShaderBuildProcess(request, stopToken);
		if (!result.IsSuccess())
		{
			return result;
		}
		ShaderLooseProgramRegistryArtifactReader registryReader{
			ShaderLooseProgramRegistryArtifactLocator(request.m_ArtifactRoot)
		};
		if (!registryReader.ReadArtifact(result.m_RegistryRef).IsSuccess())
		{
			return {
				.m_Status = DevelopmentShaderBuildStatus::ActiveRegistryUnavailable,
				.m_Diagnostics =
					"Shader build returned a RegistryId whose immutable artifact is unreadable.",
			};
		}
		return result;
	}

	DevelopmentShaderHotReloadSystem::DevelopmentShaderHotReloadSystem(
		CreateInfo createInfo) noexcept :
		m_BuildRequest(std::move(createInfo.m_BuildRequest)),
		m_TaskSystem(createInfo.m_TaskSystem),
		m_ShaderManager(createInfo.m_ShaderManager)
	{
	}

	DevelopmentShaderHotReloadSystem::~DevelopmentShaderHotReloadSystem()
	{
		Shutdown();
	}

	bool DevelopmentShaderHotReloadSystem::Initialize() noexcept
	{
		if (m_Initialized)
		{
			return true;
		}
		if (!m_BuildRequest.IsValid() || !m_TaskSystem || !m_ShaderManager)
		{
			return false;
		}
		const std::optional<uint64_t> fingerprint = ComputeSourceFingerprint();
		if (!fingerprint)
		{
			return false;
		}
		m_ObservedSourceFingerprint = *fingerprint;
		m_NextScan = std::chrono::steady_clock::now() + SourceScanInterval;
		m_Initialized = true;
		return true;
	}

	void DevelopmentShaderHotReloadSystem::Update() noexcept
	{
		if (!m_Initialized || m_ShuttingDown)
		{
			return;
		}
		TryActivatePendingRegistry();

		const auto now = std::chrono::steady_clock::now();
		if (now >= m_NextScan)
		{
			m_NextScan = now + SourceScanInterval;
			const std::optional<uint64_t> fingerprint = ComputeSourceFingerprint();
			if (fingerprint && *fingerprint != m_ObservedSourceFingerprint)
			{
				m_ObservedSourceFingerprint = *fingerprint;
				m_LastObservedChange = now;
				m_RebuildRequested = true;
			}
		}
		if (!m_BuildTask.IsValid() && m_RebuildRequested &&
			now - m_LastObservedChange >= SourceChangeDebounce)
		{
			StartBuild();
		}
	}

	void DevelopmentShaderHotReloadSystem::Shutdown() noexcept
	{
		if (m_ShuttingDown)
		{
			return;
		}
		m_ShuttingDown = true;
		if (m_TaskSystem && m_BuildTask.IsValid())
		{
			m_TaskSystem->Cancel(m_BuildTask);
		}
		m_PendingRegistry.reset();
	}

	std::optional<uint64_t>
		DevelopmentShaderHotReloadSystem::ComputeSourceFingerprint() const noexcept
	{
		try
		{
			std::vector<std::filesystem::path> files;
			std::error_code errorCode;
			for (std::filesystem::recursive_directory_iterator iterator(
				m_BuildRequest.m_ShaderSourceRoot,
				std::filesystem::directory_options::skip_permission_denied, errorCode), end;
				iterator != end; iterator.increment(errorCode))
			{
				if (errorCode)
				{
					errorCode.clear();
					continue;
				}
				if (iterator->is_regular_file(errorCode) && IsShaderSourcePath(iterator->path()))
				{
					files.push_back(iterator->path());
				}
			}
			std::ranges::sort(files);
			uint64_t hash = 14695981039346656037ull;
			for (const std::filesystem::path& file : files)
			{
				const std::filesystem::path relative =
					file.lexically_relative(m_BuildRequest.m_ShaderSourceRoot);
				const std::wstring identity = relative.generic_wstring();
				const uintmax_t size = std::filesystem::file_size(file, errorCode);
				if (errorCode)
				{
					return std::nullopt;
				}
				const auto writeTime = std::filesystem::last_write_time(file, errorCode);
				if (errorCode)
				{
					return std::nullopt;
				}
				const auto ticks = writeTime.time_since_epoch().count();
				HashBytes(hash, identity.data(), identity.size() * sizeof(wchar_t));
				HashBytes(hash, &size, sizeof(size));
				HashBytes(hash, &ticks, sizeof(ticks));
			}
			const size_t fileCount = files.size();
			HashBytes(hash, &fileCount, sizeof(fileCount));
			return hash;
		}
		catch (...)
		{
			return std::nullopt;
		}
	}

	void DevelopmentShaderHotReloadSystem::StartBuild() noexcept
	{
		m_RebuildRequested = false;
		m_BuildJob = std::make_shared<BuildJob>();
		const auto job = m_BuildJob;
		const DevelopmentShaderBuildRequest request = m_BuildRequest;
		m_BuildTask = m_TaskSystem->Submit(
			{
				.m_Name = "Shader.DevelopmentBuild",
				.m_Priority = TaskPriority::Normal,
			},
			[request, job](std::stop_token stopToken) noexcept
			{
				job->m_Result = RunDevelopmentShaderBuild(request, stopToken);
				return job->m_Result.IsSuccess()
					? TaskResult::Success()
					: TaskResult::Failure(job->m_Result.m_Diagnostics);
			},
			[this, job](const TaskCompletionInfo& completion) noexcept
			{
				m_BuildTask = {};
				if (m_ShuttingDown)
				{
					return;
				}
				if (completion.m_Status == TaskStatus::Succeeded && job->m_Result.IsSuccess())
				{
					m_PendingRegistry = job->m_Result.m_RegistryRef;
					GGLAB_LOG_INFO("Development shader build published a new registry snapshot.");
					TryActivatePendingRegistry();
				}
				else
				{
					GGLAB_LOG_WARN(
						"Development shader build failed; keeping last-known-good shaders: {}",
						job->m_Result.m_Diagnostics);
				}
			});
		if (!m_BuildTask.IsValid())
		{
			GGLAB_LOG_WARN("TaskSystem rejected the development shader build task.");
		}
	}

	void DevelopmentShaderHotReloadSystem::TryActivatePendingRegistry() noexcept
	{
		if (!m_PendingRegistry)
		{
			return;
		}
		const ShaderRegistryActivationResult activation =
			m_ShaderManager->ActivateRegistry(*m_PendingRegistry);
		if (activation.m_Status == ShaderRegistryActivationStatus::Busy)
		{
			return;
		}
		if (activation.IsSuccess())
		{
			GGLAB_LOG_INFO("Activated development shader registry (changedShaders={}).",
				activation.m_ChangedShaderCount);
		}
		else
		{
			GGLAB_LOG_WARN(
				"Rejected development shader registry; keeping last-known-good shaders: {}",
				activation.m_Error);
		}
		m_PendingRegistry.reset();
	}
}
