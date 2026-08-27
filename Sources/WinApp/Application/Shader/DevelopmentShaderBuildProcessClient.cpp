#include "Application/Shader/DevelopmentShaderBuildProcessClient.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "ShaderArtifactRuntime/ShaderCompilerProcessContract.h"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		constexpr std::chrono::milliseconds ProcessPollInterval{ 20 };
		constexpr std::chrono::seconds ProcessTimeout{ 120 };
		constexpr size_t MaximumCapturedProcessStreamSize = 64u * 1024u;

		class UniqueHandle final
		{
		public:
			UniqueHandle() = default;
			explicit UniqueHandle(HANDLE handle) noexcept : m_Handle(handle) {}
			~UniqueHandle() { Reset(); }

			UniqueHandle(const UniqueHandle&) = delete;
			UniqueHandle& operator=(const UniqueHandle&) = delete;
			UniqueHandle(UniqueHandle&& other) noexcept :
				m_Handle(std::exchange(other.m_Handle, nullptr))
			{
			}
			UniqueHandle& operator=(UniqueHandle&& other) noexcept
			{
				if (this != &other)
				{
					Reset();
					m_Handle = std::exchange(other.m_Handle, nullptr);
				}
				return *this;
			}

			[[nodiscard]] HANDLE Get() const noexcept { return m_Handle; }
			[[nodiscard]] HANDLE* Put() noexcept
			{
				Reset();
				return &m_Handle;
			}
			[[nodiscard]] HANDLE Release() noexcept
			{
				return std::exchange(m_Handle, nullptr);
			}
			void Reset(HANDLE handle = nullptr) noexcept
			{
				if (m_Handle != nullptr && m_Handle != INVALID_HANDLE_VALUE)
				{
					::CloseHandle(m_Handle);
				}
				m_Handle = handle;
			}

		private:
			HANDLE m_Handle = nullptr;
		};

		enum class ProcessExecutionStatus : uint8_t
		{
			Completed,
			LaunchFailed,
			WaitFailed,
			TimedOut,
			Cancelled,
		};

		struct ProcessExecutionResult final
		{
			ProcessExecutionStatus m_Status = ProcessExecutionStatus::LaunchFailed;
			DWORD m_ExitCode = ERROR_GEN_FAILURE;
			std::string m_StdOut{};
			std::string m_StdErr{};
			bool m_OutputTruncated = false;
		};

		[[nodiscard]] std::wstring QuoteCommandLineArgument(std::wstring_view value)
		{
			std::wstring quoted;
			quoted.reserve(value.size() + 2);
			quoted.push_back(L'"');
			size_t backslashCount = 0;
			for (wchar_t character : value)
			{
				if (character == L'\\')
				{
					++backslashCount;
					continue;
				}
				if (character == L'"')
				{
					quoted.append(backslashCount * 2 + 1, L'\\');
					quoted.push_back(character);
					backslashCount = 0;
					continue;
				}
				quoted.append(backslashCount, L'\\');
				backslashCount = 0;
				quoted.push_back(character);
			}
			quoted.append(backslashCount * 2, L'\\');
			quoted.push_back(L'"');
			return quoted;
		}

		[[nodiscard]] std::wstring QuoteCommandLineArgument(
			const std::filesystem::path& value)
		{
			const std::wstring text = value.wstring();
			return QuoteCommandLineArgument(std::wstring_view(text));
		}

		void DrainProcessStream(HANDLE readPipe, std::string& output,
			bool& outputTruncated) noexcept
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
				const DWORD requested = (std::min)(available,
					static_cast<DWORD>(buffer.size()));
				if (::ReadFile(readPipe, buffer.data(), requested, &bytesRead, nullptr) == FALSE ||
					bytesRead == 0)
				{
					return;
				}
				const size_t remaining = output.size() < MaximumCapturedProcessStreamSize
					? MaximumCapturedProcessStreamSize - output.size()
					: 0;
				output.append(buffer.data(), (std::min)(remaining,
					static_cast<size_t>(bytesRead)));
				outputTruncated |= static_cast<size_t>(bytesRead) > remaining;
			}
		}

		[[nodiscard]] ProcessExecutionResult ExecuteShaderCompiler(
			const std::filesystem::path& executable, std::wstring commandLine,
			std::stop_token stopToken) noexcept
		{
			ProcessExecutionResult result{};
			SECURITY_ATTRIBUTES securityAttributes{
				.nLength = sizeof(SECURITY_ATTRIBUTES),
				.bInheritHandle = TRUE,
			};
			UniqueHandle stdoutRead;
			UniqueHandle stdoutWrite;
			UniqueHandle stderrRead;
			UniqueHandle stderrWrite;
			if (::CreatePipe(stdoutRead.Put(), stdoutWrite.Put(), &securityAttributes, 0) == FALSE ||
				::SetHandleInformation(stdoutRead.Get(), HANDLE_FLAG_INHERIT, 0) == FALSE ||
				::CreatePipe(stderrRead.Put(), stderrWrite.Put(), &securityAttributes, 0) == FALSE ||
				::SetHandleInformation(stderrRead.Get(), HANDLE_FLAG_INHERIT, 0) == FALSE)
			{
				return result;
			}

			STARTUPINFOW startupInfo{
				.cb = sizeof(STARTUPINFOW),
				.dwFlags = STARTF_USESTDHANDLES,
				.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE),
				.hStdOutput = stdoutWrite.Get(),
				.hStdError = stderrWrite.Get(),
			};
			PROCESS_INFORMATION processInfo{};
			const BOOL created = ::CreateProcessW(executable.c_str(), commandLine.data(),
				nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
				executable.parent_path().c_str(), &startupInfo, &processInfo);
			stdoutWrite.Reset();
			stderrWrite.Reset();
			if (created == FALSE)
			{
				return result;
			}

			UniqueHandle process(processInfo.hProcess);
			UniqueHandle thread(processInfo.hThread);
			const auto deadline = std::chrono::steady_clock::now() + ProcessTimeout;
			result.m_Status = ProcessExecutionStatus::Completed;
			for (;;)
			{
				DrainProcessStream(stdoutRead.Get(), result.m_StdOut, result.m_OutputTruncated);
				DrainProcessStream(stderrRead.Get(), result.m_StdErr, result.m_OutputTruncated);
				const DWORD waitResult = ::WaitForSingleObject(process.Get(),
					static_cast<DWORD>(ProcessPollInterval.count()));
				if (waitResult == WAIT_OBJECT_0)
				{
					break;
				}
				if (waitResult == WAIT_FAILED)
				{
					result.m_Status = ProcessExecutionStatus::WaitFailed;
					break;
				}
				if (stopToken.stop_requested())
				{
					result.m_Status = ProcessExecutionStatus::Cancelled;
					break;
				}
				if (std::chrono::steady_clock::now() >= deadline)
				{
					result.m_Status = ProcessExecutionStatus::TimedOut;
					break;
				}
			}
			if (result.m_Status != ProcessExecutionStatus::Completed)
			{
				::TerminateProcess(process.Get(), ERROR_CANCELLED);
				::WaitForSingleObject(process.Get(), INFINITE);
			}
			DrainProcessStream(stdoutRead.Get(), result.m_StdOut, result.m_OutputTruncated);
			DrainProcessStream(stderrRead.Get(), result.m_StdErr, result.m_OutputTruncated);
			::GetExitCodeProcess(process.Get(), &result.m_ExitCode);
			return result;
		}

		[[nodiscard]] std::optional<nlohmann::json> ParseJsonDocumentStrict(
			std::string_view serializedDocument) noexcept
		{
			try
			{
				bool duplicateKey = false;
				std::vector<std::unordered_set<std::string>> objectKeys;
				auto callback = [&duplicateKey, &objectKeys](int /*depth*/,
					nlohmann::json::parse_event_t event, nlohmann::json& parsed) -> bool
					{
						if (event == nlohmann::json::parse_event_t::object_start)
						{
							objectKeys.emplace_back();
						}
						else if (event == nlohmann::json::parse_event_t::key)
						{
							if (objectKeys.empty() ||
								!objectKeys.back().insert(parsed.get<std::string>()).second)
							{
								duplicateKey = true;
							}
						}
						else if (event == nlohmann::json::parse_event_t::object_end)
						{
							if (objectKeys.empty())
							{
								duplicateKey = true;
							}
							else
							{
								objectKeys.pop_back();
							}
						}
						return true;
					};
				nlohmann::json document = nlohmann::json::parse(
					serializedDocument.begin(), serializedDocument.end(), callback, false, true);
				if (duplicateKey || document.is_discarded())
				{
					return std::nullopt;
				}
				return document;
			}
			catch (...)
			{
				return std::nullopt;
			}
		}

		[[nodiscard]] bool HasExactFields(const nlohmann::json& object,
			std::initializer_list<std::string_view> fields) noexcept
		{
			if (!object.is_object() || object.size() != fields.size())
			{
				return false;
			}
			for (std::string_view field : fields)
			{
				if (!object.contains(std::string(field)))
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool ReadString(const nlohmann::json& object,
			std::string_view field, std::string& outValue) noexcept
		{
			const auto found = object.find(std::string(field));
			if (found == object.end() || !found->is_string())
			{
				return false;
			}
			outValue = found->get<std::string>();
			return true;
		}

		[[nodiscard]] bool ReadBoolean(const nlohmann::json& object,
			std::string_view field, bool& outValue) noexcept
		{
			const auto found = object.find(std::string(field));
			if (found == object.end() || !found->is_boolean())
			{
				return false;
			}
			outValue = found->get<bool>();
			return true;
		}

		[[nodiscard]] bool ReadInteger(const nlohmann::json& object,
			std::string_view field, int64_t& outValue) noexcept
		{
			const auto found = object.find(std::string(field));
			if (found == object.end() || !found->is_number_integer())
			{
				return false;
			}
			if (found->is_number_unsigned())
			{
				const uint64_t value = found->get<uint64_t>();
				if (value > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()))
				{
					return false;
				}
				outValue = static_cast<int64_t>(value);
				return true;
			}
			outValue = found->get<int64_t>();
			return true;
		}

		[[nodiscard]] bool ReadDiagnostics(const nlohmann::json& object,
			std::string& outDiagnostics, size_t& outCount) noexcept
		{
			const auto found = object.find("diagnostics");
			if (found == object.end() || !found->is_array())
			{
				return false;
			}
			outCount = found->size();
			for (const nlohmann::json& diagnostic : *found)
			{
				if (!HasExactFields(diagnostic, { "message" }))
				{
					return false;
				}
				std::string message;
				if (!ReadString(diagnostic, "message", message) || message.empty())
				{
					return false;
				}
				if (!outDiagnostics.empty())
				{
					outDiagnostics += "\n";
				}
				outDiagnostics += message;
			}
			return true;
		}

		[[nodiscard]] std::optional<ShaderProgramRegistryArtifactRef> ParseRegistryId(
			std::string_view text) noexcept
		{
			if (text.size() != Sha256Digest::Size * 2)
			{
				return std::nullopt;
			}
			const auto HexValue = [](char character) noexcept -> int
				{
					if (character >= '0' && character <= '9') return character - '0';
					if (character >= 'a' && character <= 'f') return character - 'a' + 10;
					if (character >= 'A' && character <= 'F') return character - 'A' + 10;
					return -1;
				};
			ShaderProgramRegistryArtifactRef registryRef{};
			for (size_t index = 0; index < Sha256Digest::Size; ++index)
			{
				const int high = HexValue(text[index * 2]);
				const int low = HexValue(text[index * 2 + 1]);
				if (high < 0 || low < 0)
				{
					return std::nullopt;
				}
				registryRef.m_RegistryId.m_DurableDigest.m_Value[index] =
					static_cast<std::byte>((high << 4) | low);
			}
			return registryRef.IsValid()
				? std::optional(registryRef)
				: std::nullopt;
		}

		[[nodiscard]] DevelopmentShaderBuildResult MakeExecutionFailure(
			const ProcessExecutionResult& process, std::string_view operation) noexcept
		{
			switch (process.m_Status)
			{
			case ProcessExecutionStatus::LaunchFailed:
				return { .m_Status = DevelopmentShaderBuildStatus::ProcessLaunchFailed,
					.m_Diagnostics = std::format("Failed to launch gglab-shaderc {}.", operation) };
			case ProcessExecutionStatus::WaitFailed:
				return { .m_Status = DevelopmentShaderBuildStatus::ProcessFailed,
					.m_Diagnostics = std::format("Failed while waiting for gglab-shaderc {}.", operation) };
			case ProcessExecutionStatus::TimedOut:
				return { .m_Status = DevelopmentShaderBuildStatus::TimedOut,
					.m_Diagnostics = std::format("gglab-shaderc {} timed out.", operation) };
			case ProcessExecutionStatus::Cancelled:
				return { .m_Status = DevelopmentShaderBuildStatus::Cancelled,
					.m_Diagnostics = std::format("gglab-shaderc {} was cancelled.", operation) };
			case ProcessExecutionStatus::Completed:
				break;
			}
			return { .m_Status = DevelopmentShaderBuildStatus::Failed,
				.m_Diagnostics = "Unexpected shader compiler process state." };
		}
	}

	ShaderCompilerDescribeValidationResult ValidateShaderCompilerDescribeDocument(
		std::string_view serializedDocument, std::string_view requiredTarget) noexcept
	{
		ShaderCompilerDescribeValidationResult result{};
		const std::optional<nlohmann::json> document =
			ParseJsonDocumentStrict(serializedDocument);
		if (!document || !HasExactFields(*document, {
			"command", "success", "status", "exitCode", "processContractVersion",
			"toolIdentity", "toolVersion", "producerKind", "producerIdentity",
			"supportedTargets", "diagnostics" }))
		{
			result.m_Diagnostics = "describe did not return the declared JSON document shape.";
			return result;
		}

		std::string command;
		std::string status;
		std::string toolIdentity;
		std::string toolVersion;
		std::string producerKind;
		std::string producerIdentity;
		bool success = false;
		int64_t exitCode = -1;
		int64_t contractVersion = -1;
		std::string diagnostics;
		size_t diagnosticsCount = 0;
		const auto supportedTargets = document->find("supportedTargets");
		bool targetsValid = supportedTargets != document->end() &&
			supportedTargets->is_array() && !supportedTargets->empty();
		bool targetSupported = false;
		if (targetsValid)
		{
			for (const nlohmann::json& target : *supportedTargets)
			{
				if (!target.is_string())
				{
					targetsValid = false;
					break;
				}
				targetSupported |=
					target.get_ref<const std::string&>() == requiredTarget;
			}
		}

		if (!ReadString(*document, "command", command) || command != "describe" ||
			!ReadBoolean(*document, "success", success) || !success ||
			!ReadString(*document, "status", status) || status != "ok" ||
			!ReadInteger(*document, "exitCode", exitCode) || exitCode != 0 ||
			!ReadInteger(*document, "processContractVersion", contractVersion) ||
			contractVersion != ShaderProcessContractVersion ||
			!ReadString(*document, "toolIdentity", toolIdentity) ||
			toolIdentity != ShaderCompilerToolIdentity ||
			!ReadString(*document, "toolVersion", toolVersion) || toolVersion.empty() ||
			!ReadString(*document, "producerKind", producerKind) || producerKind != "dxc" ||
			!ReadString(*document, "producerIdentity", producerIdentity) ||
			producerIdentity.empty() || producerIdentity == "unknown" ||
			!targetsValid || !targetSupported ||
			!ReadDiagnostics(*document, diagnostics, diagnosticsCount) ||
			diagnosticsCount != 0)
		{
			result.m_Diagnostics =
				"describe identity, process contract, producer, or target support is incompatible.";
			return result;
		}

		result.m_Compatible = true;
		return result;
	}

	ShaderCompilerBuildDocumentResult ParseShaderCompilerBuildDocument(
		std::string_view serializedDocument, uint32_t processExitCode) noexcept
	{
		ShaderCompilerBuildDocumentResult result{};
		const std::optional<nlohmann::json> document =
			ParseJsonDocumentStrict(serializedDocument);
		if (!document || !document->is_object())
		{
			result.m_Diagnostics = "build-runtime did not return one complete JSON document.";
			return result;
		}

		bool success = false;
		std::string command;
		std::string status;
		int64_t exitCode = -1;
		int64_t programCount = -1;
		std::string diagnostics;
		size_t diagnosticsCount = 0;
		if (!ReadBoolean(*document, "success", success) ||
			!HasExactFields(*document, success
				? std::initializer_list<std::string_view>{ "command", "success", "status",
					"exitCode", "programCount", "diagnostics", "registryId" }
				: std::initializer_list<std::string_view>{ "command", "success", "status",
					"exitCode", "programCount", "diagnostics" }) ||
			!ReadString(*document, "command", command) || command != "build-runtime" ||
			!ReadString(*document, "status", status) ||
			!ReadInteger(*document, "exitCode", exitCode) || exitCode < 0 ||
			static_cast<uint64_t>(exitCode) != processExitCode ||
			!ReadInteger(*document, "programCount", programCount) || programCount < 0 ||
			!ReadDiagnostics(*document, diagnostics, diagnosticsCount))
		{
			result.m_Diagnostics = "build-runtime JSON fields disagree with the process contract.";
			return result;
		}

		if (!success)
		{
			if (processExitCode == 0 || status != "failed" || diagnosticsCount == 0)
			{
				result.m_Diagnostics = "build-runtime failure envelope is internally inconsistent.";
				return result;
			}
			result.m_ProtocolValid = true;
			result.m_Diagnostics = std::move(diagnostics);
			return result;
		}

		std::string registryId;
		const std::optional<ShaderProgramRegistryArtifactRef> registryRef =
			ReadString(*document, "registryId", registryId)
				? ParseRegistryId(registryId)
				: std::nullopt;
		if (processExitCode != 0 || status != "ok" || programCount == 0 ||
			diagnosticsCount != 0 || !registryRef)
		{
			result.m_Diagnostics = "build-runtime success envelope is internally inconsistent.";
			return result;
		}
		result.m_ProtocolValid = true;
		result.m_Succeeded = true;
		result.m_RegistryRef = *registryRef;
		return result;
	}

	DevelopmentShaderBuildResult RunDevelopmentShaderBuildProcess(
		const DevelopmentShaderBuildRequest& request, std::stop_token stopToken) noexcept
	{
		try
		{
			const std::string_view target = request.m_ActiveBackend == RHIBackendType::Vulkan
				? "gglab-vulkan13"
				: "gglab-dx12";
			const std::wstring executableArgument =
				QuoteCommandLineArgument(request.m_ShaderCompilerPath);
			const ProcessExecutionResult describe = ExecuteShaderCompiler(
				request.m_ShaderCompilerPath, executableArgument + L" describe", stopToken);
			if (describe.m_Status != ProcessExecutionStatus::Completed)
			{
				return MakeExecutionFailure(describe, "describe handshake");
			}
			if (describe.m_OutputTruncated || !describe.m_StdErr.empty())
			{
				return { .m_Status = DevelopmentShaderBuildStatus::ToolIncompatible,
					.m_Diagnostics = "describe violated the machine stdout/stderr channel contract." };
			}
			const ShaderCompilerDescribeValidationResult compatibility =
				ValidateShaderCompilerDescribeDocument(describe.m_StdOut, target);
			if (describe.m_ExitCode != 0)
			{
				return { .m_Status = DevelopmentShaderBuildStatus::ProcessFailed,
					.m_Diagnostics = describe.m_StdOut.empty()
						? std::format("gglab-shaderc describe failed with exit code {}.",
							describe.m_ExitCode)
						: describe.m_StdOut };
			}
			if (!compatibility.m_Compatible)
			{
				return { .m_Status = DevelopmentShaderBuildStatus::ToolIncompatible,
					.m_Diagnostics = compatibility.m_Diagnostics };
			}

			const std::wstring wideTarget(target.begin(), target.end());
			std::wstring commandLine = executableArgument +
				L" build-runtime --source-root " +
				QuoteCommandLineArgument(request.m_ShaderSourceRoot) +
				L" --target " + QuoteCommandLineArgument(std::wstring_view(wideTarget)) +
				L" --cache-root " + QuoteCommandLineArgument(request.m_ShaderCacheRoot) +
				L" --artifact-root " + QuoteCommandLineArgument(request.m_ArtifactRoot) +
				L" --result-format json";
			const ProcessExecutionResult build = ExecuteShaderCompiler(
				request.m_ShaderCompilerPath, std::move(commandLine), stopToken);
			if (build.m_Status != ProcessExecutionStatus::Completed)
			{
				return MakeExecutionFailure(build, "build-runtime");
			}
			if (build.m_OutputTruncated || !build.m_StdErr.empty())
			{
				return { .m_Status = DevelopmentShaderBuildStatus::ToolIncompatible,
					.m_Diagnostics = "build-runtime violated the machine stdout/stderr channel contract." };
			}
			const ShaderCompilerBuildDocumentResult parsed =
				ParseShaderCompilerBuildDocument(build.m_StdOut, build.m_ExitCode);
			if (!parsed.m_ProtocolValid)
			{
				return { .m_Status = DevelopmentShaderBuildStatus::ToolIncompatible,
					.m_Diagnostics = parsed.m_Diagnostics };
			}
			if (!parsed.m_Succeeded)
			{
				return { .m_Status = DevelopmentShaderBuildStatus::ProcessFailed,
					.m_Diagnostics = parsed.m_Diagnostics };
			}
			return {
				.m_Status = DevelopmentShaderBuildStatus::Succeeded,
				.m_RegistryRef = parsed.m_RegistryRef,
			};
		}
		catch (...)
		{
			return {
				.m_Status = DevelopmentShaderBuildStatus::Failed,
				.m_Diagnostics = "External shader compiler process client failed unexpectedly.",
			};
		}
	}
}
