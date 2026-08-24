#include "Application/Shader/DevelopmentShaderBuildBridge.h"
#include "AppRuntimeLog.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
		constexpr std::chrono::milliseconds ProcessPollInterval{ 20 };
		constexpr std::chrono::seconds ProcessTimeout{ 120 };
		constexpr std::chrono::milliseconds SourceScanInterval{ 250 };
		constexpr std::chrono::milliseconds SourceChangeDebounce{ 200 };
		constexpr size_t MaximumCapturedDiagnosticsSize = 64u * 1024u;

		[[nodiscard]] std::wstring QuoteCommandLineArgument(
			const std::filesystem::path& value)
		{
			return L"\"" + value.wstring() + L"\"";
		}

		void DrainProcessOutput(HANDLE readPipe, std::string& output) noexcept
		{
			std::array<char, 4'096> buffer{};
			for (;;)
			{
				DWORD available = 0;
				if (::PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) == FALSE ||
					available == 0)
				{
					return;
				}
				DWORD bytesRead = 0;
				const DWORD requested = (std::min)(
					available, static_cast<DWORD>(buffer.size()));
				if (::ReadFile(readPipe, buffer.data(), requested, &bytesRead, nullptr) == FALSE ||
					bytesRead == 0)
				{
					return;
				}
				if (output.size() < MaximumCapturedDiagnosticsSize)
				{
					const size_t remaining = MaximumCapturedDiagnosticsSize - output.size();
					output.append(buffer.data(), (std::min)(remaining,
						static_cast<size_t>(bytesRead)));
				}
			}
		}

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

		[[nodiscard]] std::optional<uint8_t> ParseHexNibble(char character) noexcept
		{
			if (character >= '0' && character <= '9')
			{
				return static_cast<uint8_t>(character - '0');
			}
			if (character >= 'a' && character <= 'f')
			{
				return static_cast<uint8_t>(character - 'a' + 10);
			}
			if (character >= 'A' && character <= 'F')
			{
				return static_cast<uint8_t>(character - 'A' + 10);
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<ShaderProgramRegistryArtifactRef>
			ParseRegistryRefFromBuildOutput(std::string_view output) noexcept
		{
			constexpr std::string_view FieldName = "\"registryId\"";
			const size_t field = output.find(FieldName);
			if (field == std::string_view::npos)
			{
				return std::nullopt;
			}
			const size_t colon = output.find(':', field + FieldName.size());
			const size_t quote = colon == std::string_view::npos
				? std::string_view::npos
				: output.find('\"', colon + 1);
			if (quote == std::string_view::npos ||
				output.size() - quote - 1 < Sha256Digest::Size * 2)
			{
				return std::nullopt;
			}

			ShaderProgramRegistryArtifactRef registryRef{};
			for (size_t byteIndex = 0; byteIndex < Sha256Digest::Size; ++byteIndex)
			{
				const auto high = ParseHexNibble(output[quote + 1 + byteIndex * 2]);
				const auto low = ParseHexNibble(output[quote + 2 + byteIndex * 2]);
				if (!high || !low)
				{
					return std::nullopt;
				}
				registryRef.m_RegistryId.m_DurableDigest.m_Value[byteIndex] =
					static_cast<std::byte>((*high << 4u) | *low);
			}
			const size_t closingQuote = quote + 1 + Sha256Digest::Size * 2;
			if (closingQuote >= output.size() || output[closingQuote] != '\"' ||
				!registryRef.IsValid())
			{
				return std::nullopt;
			}
			return registryRef;
		}
	}

	bool DevelopmentShaderBuildRequest::IsValid() const noexcept
	{
		return m_ActiveBackend != RHIBackendType::Unknown &&
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

		try
		{
			std::error_code errorCode;
			if (!std::filesystem::is_regular_file(request.m_ShaderCompilerPath, errorCode))
			{
				return {
					.m_Status = DevelopmentShaderBuildStatus::ToolNotFound,
					.m_Diagnostics = "gglab-shaderc.exe was not found beside the host executable.",
				};
			}

			SECURITY_ATTRIBUTES securityAttributes{
				.nLength = sizeof(SECURITY_ATTRIBUTES),
				.bInheritHandle = TRUE,
			};
			HANDLE readPipe = nullptr;
			HANDLE writePipe = nullptr;
			if (::CreatePipe(&readPipe, &writePipe, &securityAttributes, 0) == FALSE)
			{
				return {
					.m_Status = DevelopmentShaderBuildStatus::ProcessLaunchFailed,
					.m_Diagnostics = "Failed to create the shader build process output pipe.",
				};
			}
			::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

			const wchar_t* target = request.m_ActiveBackend == RHIBackendType::Vulkan
				? L"gglab-vulkan13"
				: L"gglab-dx12";
			std::wstring commandLine = QuoteCommandLineArgument(request.m_ShaderCompilerPath) +
				L" build-runtime --source-root " + QuoteCommandLineArgument(request.m_ShaderSourceRoot) +
				L" --target \"" + target + L"\" --cache-root " +
				QuoteCommandLineArgument(request.m_ShaderCacheRoot) +
				L" --artifact-root " + QuoteCommandLineArgument(request.m_ArtifactRoot) +
				L" --result-format json";
			STARTUPINFOW startupInfo{
				.cb = sizeof(STARTUPINFOW),
				.dwFlags = STARTF_USESTDHANDLES,
				.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE),
				.hStdOutput = writePipe,
				.hStdError = writePipe,
			};
			PROCESS_INFORMATION processInfo{};
			const BOOL created = ::CreateProcessW(
				request.m_ShaderCompilerPath.c_str(), commandLine.data(), nullptr, nullptr,
				TRUE, CREATE_NO_WINDOW, nullptr,
				request.m_ShaderCompilerPath.parent_path().c_str(),
				&startupInfo, &processInfo);
			::CloseHandle(writePipe);
			if (created == FALSE)
			{
				::CloseHandle(readPipe);
				return {
					.m_Status = DevelopmentShaderBuildStatus::ProcessLaunchFailed,
					.m_Diagnostics = "Failed to launch gglab-shaderc.exe.",
				};
			}

			std::string diagnostics;
			const auto deadline = std::chrono::steady_clock::now() + ProcessTimeout;
			DevelopmentShaderBuildStatus interruptedStatus =
				DevelopmentShaderBuildStatus::Succeeded;
			for (;;)
			{
				DrainProcessOutput(readPipe, diagnostics);
				const DWORD waitResult = ::WaitForSingleObject(
					processInfo.hProcess, static_cast<DWORD>(ProcessPollInterval.count()));
				if (waitResult == WAIT_OBJECT_0)
				{
					break;
				}
				if (stopToken.stop_requested())
				{
					interruptedStatus = DevelopmentShaderBuildStatus::Cancelled;
					break;
				}
				if (std::chrono::steady_clock::now() >= deadline)
				{
					interruptedStatus = DevelopmentShaderBuildStatus::TimedOut;
					break;
				}
			}
			if (interruptedStatus != DevelopmentShaderBuildStatus::Succeeded)
			{
				::TerminateProcess(processInfo.hProcess, ERROR_CANCELLED);
				::WaitForSingleObject(processInfo.hProcess, INFINITE);
			}
			DrainProcessOutput(readPipe, diagnostics);
			DWORD exitCode = ERROR_GEN_FAILURE;
			::GetExitCodeProcess(processInfo.hProcess, &exitCode);
			::CloseHandle(processInfo.hThread);
			::CloseHandle(processInfo.hProcess);
			::CloseHandle(readPipe);

			if (interruptedStatus != DevelopmentShaderBuildStatus::Succeeded)
			{
				return {
					.m_Status = interruptedStatus,
					.m_Diagnostics = interruptedStatus == DevelopmentShaderBuildStatus::TimedOut
						? "External shader build timed out."
						: "External shader build was cancelled.",
				};
			}
			if (exitCode != 0)
			{
				return {
					.m_Status = DevelopmentShaderBuildStatus::ProcessFailed,
					.m_Diagnostics = diagnostics.empty()
						? std::format("gglab-shaderc failed with exit code {}.", exitCode)
						: std::move(diagnostics),
				};
			}

			const std::optional<ShaderProgramRegistryArtifactRef> registryRef =
				ParseRegistryRefFromBuildOutput(diagnostics);
			ShaderLooseProgramRegistryArtifactReader registryReader{
				ShaderLooseProgramRegistryArtifactLocator(request.m_ArtifactRoot)
			};
			if (!registryRef || !registryReader.ReadArtifact(*registryRef).IsSuccess())
			{
				return {
					.m_Status = DevelopmentShaderBuildStatus::ActiveRegistryUnavailable,
					.m_Diagnostics =
						"Shader build succeeded without an exact readable RegistryId result.",
				};
			}
			return {
				.m_Status = DevelopmentShaderBuildStatus::Succeeded,
				.m_RegistryRef = *registryRef,
				.m_Diagnostics = std::move(diagnostics),
			};
		}
		catch (...)
		{
			return {
				.m_Status = DevelopmentShaderBuildStatus::Failed,
				.m_Diagnostics = "External shader build failed unexpectedly.",
			};
		}
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
