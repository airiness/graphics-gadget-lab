#include "ShaderCompilerCliContractSelfTests.h"
#include "ShaderArtifactRuntime/ShaderCompilerProcessContract.h"
#include "Artifact/ShaderArtifactManifestIO.h"
#include "Compiler/ShaderCompiler.h"
#include "Contracts/ShaderArtifact.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "GGLabTestCore/SelfTest.h"
#include "DevelopmentShaderPaths.h"
#include "ShaderArtifactRuntime/ShaderArtifactStore.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"
#include "Targets/DX12ShaderTarget.h"
#include "Targets/Vulkan13ShaderTarget.h"
#include "Targets/ShaderTargetWireNames.h"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		class ScopedTestDirectory
		{
		public:
			explicit ScopedTestDirectory(std::filesystem::path path) noexcept : m_Path(std::move(path))
			{
				std::error_code errorCode;
				std::filesystem::remove_all(m_Path, errorCode);
			}
			~ScopedTestDirectory()
			{
				std::error_code errorCode;
				std::filesystem::remove_all(m_Path, errorCode);
			}

			const std::filesystem::path& GetPath() const noexcept { return m_Path; }

		private:
			std::filesystem::path m_Path;
		};

		struct CliRunResult
		{
			int m_ExitCode = -1;
			std::string m_StdOut{};
			std::string m_StdErr{};
		};

		class ScopedEnvironmentVariable
		{
		public:
			ScopedEnvironmentVariable(std::wstring name, std::wstring_view value) noexcept :
				m_Name(std::move(name))
			{
				SetLastError(ERROR_SUCCESS);
				const DWORD required = GetEnvironmentVariableW(m_Name.c_str(), nullptr, 0);
				if (required > 0)
				{
					std::vector<wchar_t> previous(required);
					if (GetEnvironmentVariableW(m_Name.c_str(), previous.data(), required) > 0)
					{
						m_HadPreviousValue = true;
						m_PreviousValue = previous.data();
					}
				}
				else if (GetLastError() == ERROR_SUCCESS)
				{
					m_HadPreviousValue = true;
				}
				m_Set = SetEnvironmentVariableW(m_Name.c_str(), std::wstring(value).c_str()) != FALSE;
			}

			~ScopedEnvironmentVariable()
			{
				if (m_Set)
				{
					SetEnvironmentVariableW(m_Name.c_str(),
						m_HadPreviousValue ? m_PreviousValue.c_str() : nullptr);
				}
			}

			ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
			ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;
			ScopedEnvironmentVariable(ScopedEnvironmentVariable&&) = delete;
			ScopedEnvironmentVariable& operator=(ScopedEnvironmentVariable&&) = delete;

			[[nodiscard]] bool IsSet() const noexcept { return m_Set; }

		private:
			std::wstring m_Name{};
			std::wstring m_PreviousValue{};
			bool m_HadPreviousValue = false;
			bool m_Set = false;
		};

		[[nodiscard]] std::filesystem::path ShaderCompilerExecutablePath() noexcept
		{
			return win32::GetExecutableDirectory() /
				(utils::ToWideString(ShaderCompilerToolIdentity) + L".exe");
		}

		[[nodiscard]] CliRunResult RunCli(const std::vector<std::wstring>& arguments,
			bool forceCompilerUnavailable = false,
			bool forceDescribeInternalError = false,
			const std::wstring& workingDirectory = std::wstring()) noexcept
		{
			CliRunResult result{};
			std::optional<ScopedEnvironmentVariable> compilerUnavailableEnvironment;
			if (forceCompilerUnavailable)
			{
				compilerUnavailableEnvironment.emplace(
					L"GGLAB_SHADERC_TEST_FORCE_COMPILER_UNAVAILABLE", L"1");
				if (!compilerUnavailableEnvironment->IsSet())
				{
					return result;
				}
			}
			std::optional<ScopedEnvironmentVariable> describeInternalErrorEnvironment;
			if (forceDescribeInternalError)
			{
				describeInternalErrorEnvironment.emplace(
					L"GGLAB_SHADERC_TEST_FORCE_DESCRIBE_INTERNAL_ERROR", L"1");
				if (!describeInternalErrorEnvironment->IsSet())
				{
					return result;
				}
			}
			SECURITY_ATTRIBUTES securityAttributes{
				.nLength = sizeof(SECURITY_ATTRIBUTES),
				.bInheritHandle = TRUE,
			};
			HANDLE outRead = nullptr;
			HANDLE outWrite = nullptr;
			HANDLE errRead = nullptr;
			HANDLE errWrite = nullptr;
			if (!CreatePipe(&outRead, &outWrite, &securityAttributes, 0) ||
				!CreatePipe(&errRead, &errWrite, &securityAttributes, 0))
			{
				return result;
			}
			SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
			SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

			std::wstring commandLine;
			commandLine += L"\"";
			commandLine += ShaderCompilerExecutablePath().wstring();
			commandLine += L"\"";
			for (const std::wstring& argument : arguments)
			{
				commandLine += L" \"";
				commandLine += argument;
				commandLine += L"\"";
			}

			STARTUPINFOW startupInfo{
				.cb = sizeof(STARTUPINFOW),
				.dwFlags = STARTF_USESTDHANDLES,
				.hStdOutput = outWrite,
				.hStdError = errWrite,
			};
			PROCESS_INFORMATION processInfo{};
			const std::wstring exePath = ShaderCompilerExecutablePath().wstring();
			const std::wstring exeDirectory = win32::GetExecutableDirectory().wstring();
			const std::wstring& currentDirectory =
				workingDirectory.empty() ? exeDirectory : workingDirectory;
			const BOOL created = CreateProcessW(
				exePath.c_str(),
				commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
				nullptr, currentDirectory.c_str(), &startupInfo, &processInfo);
			compilerUnavailableEnvironment.reset();
			describeInternalErrorEnvironment.reset();
			CloseHandle(outWrite);
			CloseHandle(errWrite);
			if (!created)
			{
				CloseHandle(outRead);
				CloseHandle(errRead);
				return result;
			}

			const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 60'000);
			if (waitResult == WAIT_TIMEOUT)
			{
				TerminateProcess(processInfo.hProcess, ERROR_TIMEOUT);
				WaitForSingleObject(processInfo.hProcess, INFINITE);
			}

			std::array<char, 4'096> buffer{};
			DWORD bytesRead = 0;
			while (ReadFile(outRead, buffer.data(), static_cast<DWORD>(buffer.size()),
				&bytesRead, nullptr) && bytesRead > 0)
			{
				result.m_StdOut.append(buffer.data(), bytesRead);
			}
			while (ReadFile(errRead, buffer.data(), static_cast<DWORD>(buffer.size()),
				&bytesRead, nullptr) && bytesRead > 0)
			{
				result.m_StdErr.append(buffer.data(), bytesRead);
			}

			DWORD exitCode = ERROR_GEN_FAILURE;
			GetExitCodeProcess(processInfo.hProcess, &exitCode);
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			CloseHandle(outRead);
			CloseHandle(errRead);
			result.m_ExitCode = static_cast<int>(exitCode);
			return result;
		}

		[[nodiscard]] bool IsSingleJsonDocument(const CliRunResult& result) noexcept
		{
			if (!result.m_StdErr.empty() || result.m_StdOut.empty())
			{
				return false;
			}
			std::string_view document = result.m_StdOut;
			if (document.ends_with('\n'))
			{
				document.remove_suffix(1);
			}
			if (document.ends_with('\r'))
			{
				document.remove_suffix(1);
			}
			return document.size() >= 2 && document.front() == '{' && document.back() == '}' &&
				document.find('\n') == std::string_view::npos &&
				document.find('\r') == std::string_view::npos;
		}

		// Minimal semver (MAJOR.MINOR.PATCH, non-negative integers) shape check
		// for the describe toolVersion wire fact.
		[[nodiscard]] bool IsSemver(std::string_view text) noexcept
		{
			if (text.empty() || text.front() < '0' || text.front() > '9')
			{
				return false;
			}
			std::size_t dots = 0;
			bool inDigitRun = false;
			for (const char character : text)
			{
				if (character >= '0' && character <= '9')
				{
					inDigitRun = true;
					continue;
				}
				if (character == '.' && inDigitRun)
				{
					++dots;
					inDigitRun = false;
					continue;
				}
				return false;
			}
			return inDigitRun && dots == 2;
		}

		// Parse the describe wire output as JSON. Returns std::nullopt when the
		// text is not a valid JSON object, so a caller can distinguish an
		// unparsable document from a well-formed document with the wrong fields.
		[[nodiscard]] std::optional<nlohmann::json> ParseWireDocument(
			std::string_view text) noexcept
		{
			try
			{
				const nlohmann::json document =
					nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
				if (document.is_discarded() || !document.is_object())
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

		// Safe typed reads for the describe wire: a missing key or a type
		// mismatch is a failed read, never an exception. A malformed document
		// must fail the check that reads it, not terminate the test process.
		[[nodiscard]] bool ReadWireString(std::optional<nlohmann::json> doc,
			std::string_view key, std::string& out) noexcept
		{
			if (!doc.has_value() || !doc->contains(key) || !doc->at(key).is_string())
			{
				return false;
			}
			out = doc->at(key).get<std::string>();
			return true;
		}

		[[nodiscard]] bool ReadWireInteger(std::optional<nlohmann::json> doc,
			std::string_view key, std::int64_t& out) noexcept
		{
			if (!doc.has_value() || !doc->contains(key) ||
				!doc->at(key).is_number_integer())
			{
				return false;
			}
			out = doc->at(key).get<std::int64_t>();
			return true;
		}

		[[nodiscard]] bool ReadWireBoolean(std::optional<nlohmann::json> doc,
			std::string_view key, bool& out) noexcept
		{
			if (!doc.has_value() || !doc->contains(key) || !doc->at(key).is_boolean())
			{
				return false;
			}
			out = doc->at(key).get<bool>();
			return true;
		}

		[[nodiscard]] bool WireDiagnosticsCount(std::optional<nlohmann::json> doc,
			std::size_t& count) noexcept
		{
			if (!doc.has_value() || !doc->contains("diagnostics") ||
				!doc->at("diagnostics").is_array())
			{
				return false;
			}
			count = doc->at("diagnostics").size();
			return true;
		}

		[[nodiscard]] bool HasJsonEnvelope(const CliRunResult& result,
			std::string_view status, int exitCode, bool success,
			std::string_view command = "compile") noexcept
		{
			return result.m_ExitCode == exitCode && IsSingleJsonDocument(result) &&
				result.m_StdOut.find(std::format("\"command\":\"{}\"", command)) !=
					std::string::npos &&
				result.m_StdOut.find(success ? "\"success\":true" : "\"success\":false") !=
					std::string::npos &&
				result.m_StdOut.find(std::format("\"status\":\"{}\"", status)) !=
					std::string::npos &&
				result.m_StdOut.find(std::format("\"exitCode\":{}", exitCode)) !=
					std::string::npos &&
				result.m_StdOut.find("\"diagnostics\":[") != std::string::npos;
		}

		[[nodiscard]] bool SurfaceDescriptorToolIdentitiesMatch(
			const std::filesystem::path& sourceRoot,
			std::string_view expectedIdentity) noexcept
		{
			bool foundDescriptor = false;
			const std::filesystem::path surfaceProfilesRoot =
				sourceRoot / L"Profiles" / L"GGLab.Surface";
			std::error_code errorCode;
			std::filesystem::recursive_directory_iterator iterator(
				surfaceProfilesRoot,
				std::filesystem::directory_options::skip_permission_denied, errorCode);
			const std::filesystem::recursive_directory_iterator end;
			if (errorCode)
			{
				return false;
			}

			for (; iterator != end; iterator.increment(errorCode))
			{
				if (errorCode)
				{
					return false;
				}
				if (!iterator->is_regular_file(errorCode) || errorCode ||
					iterator->path().filename() != L"descriptor.json")
				{
					if (errorCode)
					{
						return false;
					}
					continue;
				}

				std::ifstream input(iterator->path(), std::ios::binary);
				const nlohmann::json descriptor =
					nlohmann::json::parse(input, nullptr, /*allow_exceptions=*/false);
				if (!input || descriptor.is_discarded() || !descriptor.is_object() ||
					!descriptor.contains("processContract") ||
					!descriptor.at("processContract").is_object())
				{
					return false;
				}
				const nlohmann::json& processContract = descriptor.at("processContract");
				if (!processContract.contains("tool") ||
					!processContract.at("tool").is_object())
				{
					return false;
				}
				const nlohmann::json& tool = processContract.at("tool");
				if (!tool.contains("identity") || !tool.at("identity").is_string() ||
					tool.at("identity").get<std::string>() != expectedIdentity)
				{
					return false;
				}
				foundDescriptor = true;
			}
			return foundDescriptor;
		}

		[[nodiscard]] std::string ExtractJsonField(
			std::string_view json, std::string_view fieldName) noexcept
		{
			const std::string prefix = std::string("\"") + std::string(fieldName) + "\":\"";
			const std::size_t begin = json.find(prefix);
			if (begin == std::string_view::npos)
			{
				return {};
			}
			const std::size_t valueBegin = begin + prefix.size();
			const std::size_t end = json.find('"', valueBegin);
			if (end == std::string_view::npos)
			{
				return {};
			}
			const std::string_view encoded = json.substr(valueBegin, end - valueBegin);
			std::string decoded;
			decoded.reserve(encoded.size());
			for (std::size_t index = 0; index < encoded.size(); ++index)
			{
				if (encoded[index] != '\\' || index + 1 >= encoded.size())
				{
					decoded += encoded[index];
					continue;
				}
				const char escaped = encoded[++index];
				switch (escaped)
				{
				case '\\':
					decoded += '\\';
					break;
				case '"':
					decoded += '"';
					break;
				case 'n':
					decoded += '\n';
					break;
				case 'r':
					decoded += '\r';
					break;
				case 't':
					decoded += '\t';
					break;
				default:
					return {};
				}
			}
			return decoded;
		}

		void RunDescribeHandshakeTests(SelfTestContext& context,
			const std::filesystem::path& sourceRoot,
			const std::filesystem::path& tempRoot) noexcept
		{
			// Machine describe handshake contract — wire verification. These
			// protocol self-tests go only through describe (per the consumer
			// discipline): targets / --version stay state-behavior checks and are
			// not consumer-facing contract verdicts.

			// Byte stability, a parsed JSON object document, and exact wire fields.
			{
				const CliRunResult first = RunCli({ L"describe" });
				const CliRunResult second = RunCli({ L"describe" });
				const auto firstDoc = ParseWireDocument(first.m_StdOut);
				const auto secondDoc = ParseWireDocument(second.m_StdOut);
				context.Check(
					first.m_ExitCode == 0 && second.m_ExitCode == 0 &&
						first.m_StdOut == second.m_StdOut &&
						first.m_StdErr.empty() && second.m_StdErr.empty() &&
						firstDoc.has_value() && secondDoc.has_value() &&
						*firstDoc == *secondDoc,
					"describe emits a byte-stable parsed JSON object document and no stderr");
				std::string command;
				std::string status;
				bool success = false;
				std::int64_t exitCode = -1;
				std::int64_t contractVersion = -1;
				std::size_t diagnosticsCount = 0;
				context.Check(
					ReadWireString(firstDoc, "command", command) &&
						command == "describe" &&
						ReadWireBoolean(firstDoc, "success", success) && success &&
						ReadWireString(firstDoc, "status", status) && status == "ok" &&
						ReadWireInteger(firstDoc, "exitCode", exitCode) && exitCode == 0 &&
						ReadWireInteger(firstDoc, "processContractVersion", contractVersion) &&
							contractVersion == gglab::ShaderProcessContractVersion &&
						WireDiagnosticsCount(firstDoc, diagnosticsCount) &&
							diagnosticsCount == 0,
					"describe.success carries exact command/success/status/exitCode and an empty diagnostics array");
			}

			// Identity chains: the wire tool identity equals every published
			// Surface descriptor requirement, while producer identity equals the
			// in-process canonical DXC identity (never the "unknown" sentinel).
			{
				const CliRunResult result = RunCli({ L"describe" });
				const auto doc = ParseWireDocument(result.m_StdOut);
				const ShaderCompilerIdentity inProcess = QueryDxcCompilerIdentity();
				std::string wireToolIdentity;
				std::string wireProducerIdentity;
				const bool toolChain = ReadWireString(doc, "toolIdentity", wireToolIdentity) &&
					wireToolIdentity == std::string(ShaderCompilerToolIdentity) &&
					SurfaceDescriptorToolIdentitiesMatch(
						sourceRoot, wireToolIdentity);
				const bool producerChain = ReadWireString(
						doc, "producerIdentity", wireProducerIdentity) &&
					wireProducerIdentity == utils::ToString(inProcess.m_CanonicalIdentity) &&
					!wireProducerIdentity.empty() && wireProducerIdentity != "unknown";
				context.Check(
					toolChain && producerChain,
					"describe tool identity matches every Surface descriptor and producer identity matches the canonical DXC identity");
			}

			// toolVersion matches the current tool-version constant (not a
			// hard-coded value) and is semver-shaped; the concrete
			// version-comparison rule is the consumer's discipline, not this
			// wire's.
			{
				const CliRunResult result = RunCli({ L"describe" });
				const auto doc = ParseWireDocument(result.m_StdOut);
				std::string wireVersion;
				const bool versionRead = ReadWireString(doc, "toolVersion", wireVersion);
				context.Check(
					versionRead &&
						wireVersion == utils::ToString(ShaderCompilerToolVersion) &&
						IsSemver(wireVersion),
					"describe.toolVersion matches the tool-version constant and is semver-shaped");
			}

			// processContractVersion carries the declared process contract
			// value, compared to the constant (never a hard-coded wire).
			{
				const CliRunResult result = RunCli({ L"describe" });
				const auto doc = ParseWireDocument(result.m_StdOut);
				std::int64_t contractVersion = -1;
				const bool versionRead = ReadWireInteger(
					doc, "processContractVersion", contractVersion);
				context.Check(
					versionRead &&
						contractVersion == gglab::ShaderProcessContractVersion,
					"describe.processContractVersion equals the declared process contract value");
			}

			// Producer kind parity: the in-process descriptor kind maps to
			// exactly its wire name (dxc), never a fallback.
			{
				const CliRunResult result = RunCli({ L"describe" });
				const auto doc = ParseWireDocument(result.m_StdOut);
				const ShaderCompilerIdentity inProcess = QueryDxcCompilerIdentity();
				std::string wireKind;
				const bool kindRead = ReadWireString(doc, "producerKind", wireKind);
				context.Check(
					inProcess.m_Kind == ShaderCompilerKind::Dxc &&
						kindRead && wireKind == "dxc",
					"describe producer kind parity with the in-process descriptor kind (dxc)");
			}

			// supportedTargets is exactly the supported set as declared by the
			// single-table authority (accept + report both derived from kEntries).
			{
				const CliRunResult result = RunCli({ L"describe" });
				const auto doc = ParseWireDocument(result.m_StdOut);
				const std::vector<std::string> wireNames = ShaderTargetWire::Names();
				ShaderTargetProfile parsedProfile = ShaderTargetProfile::GGLabDX12;
				const bool parseDx12 = ShaderTargetWire::Parse("gglab-dx12", parsedProfile) &&
					parsedProfile == ShaderTargetProfile::GGLabDX12;
				const bool parseVulkan = ShaderTargetWire::Parse("gglab-vulkan13", parsedProfile) &&
					parsedProfile == ShaderTargetProfile::GGLabVulkan13;
				context.Check(
					wireNames.size() == 2 &&
						parseDx12 && parseVulkan,
					"target wire-name single-table authority enumerates and accepts the supported set");
				const bool targetsMatches = doc
					? doc->contains("supportedTargets") &&
						doc->at("supportedTargets").is_array() &&
						doc->at("supportedTargets") == nlohmann::json(wireNames)
					: false;
				context.Check(
					targetsMatches,
					"describe.supportedTargets exactly equals the single-table authority (no extras)");
			}

			// No caller build context appears in the describe result, and the
			// result is byte-identical even when run from a foreign working
			// directory (a real cwd change, not a field-absence assumption).
			{
				const std::filesystem::path decoyRoot = tempRoot / L"DescribeCwd";
				std::error_code errorCode;
				std::filesystem::create_directories(decoyRoot, errorCode);
				// A decoy that would surface in the wire if the working
				// directory leaked into the describe document.
				bool decoyWritten = false;
				{
					std::ofstream output(decoyRoot / L"source-root.hlsl", std::ios::binary);
					output << "// decoy";
					decoyWritten = output.good();
				}
				const CliRunResult reference = RunCli({ L"describe" });
				const CliRunResult fromDecoy = RunCli(
					{ L"describe" }, false, false, decoyRoot.wstring());
				const auto doc = ParseWireDocument(fromDecoy.m_StdOut);
				const char* const callerContextFields[] = {
					"sourceRoot", "source-root", "sourcePath", "cacheRoot", "cache-root",
					"artifactRoot", "artifact-root", "binaryPath", "cacheRecordPath", "cwd",
				};
				bool absent = doc.has_value();
				for (const char* const field : callerContextFields)
				{
					absent = absent && !doc->contains(field);
				}
				context.Check(
					!errorCode && decoyWritten &&
						reference.m_ExitCode == 0 && fromDecoy.m_ExitCode == 0 &&
						reference.m_StdErr.empty() && fromDecoy.m_StdErr.empty() &&
						reference.m_StdOut == fromDecoy.m_StdOut &&
						absent,
					"describe carries no caller build context and is byte-identical from a foreign cwd");
			}

			// stdout purity: the channel framing holds (one single-line JSON
			// object on stdout, empty stderr) and the document parses.
			{
				const CliRunResult result = RunCli({ L"describe" });
				const auto doc = ParseWireDocument(result.m_StdOut);
				context.Check(
					IsSingleJsonDocument(result) && doc.has_value(),
					"describe success keeps one-JSON-on-stdout channel framing and parses");
			}

			// usage errors are structured (three variants), no business payload.
			{
				const CliRunResult positional = RunCli({ L"describe", L"extra" });
				const CliRunResult resultFormat = RunCli({ L"describe", L"--result-format", L"json" });
				const CliRunResult bogusFlag = RunCli({ L"describe", L"--bogus" });
				// (The original substring-based lambda was replaced by the
				// parse-based CheckDescribeUsageError below.)
				
				// Each usage-error response must be a fully parsed describe document
				// with the failure wire shape and no business-payload fields.
				auto CheckDescribeUsageError = [&](
					const CliRunResult& run, const char* label) noexcept
				{
					const std::string checkName =
						std::string("describe usage-error structured (") + label + ")";
					const auto doc = ParseWireDocument(run.m_StdOut);
					std::string command;
					std::string status;
					bool success = true;
					std::int64_t exitCode = -1;
					std::int64_t contractVersion = -1;
					std::size_t diagnosticsCount = 0;
					const bool structuredFailure =
						ReadWireString(doc, "command", command) &&
							command == "describe" &&
							ReadWireBoolean(doc, "success", success) && !success &&
							ReadWireString(doc, "status", status) &&
								status == "usage-error" &&
							ReadWireInteger(doc, "exitCode", exitCode) && exitCode == 2 &&
							ReadWireInteger(
								doc, "processContractVersion", contractVersion) &&
								contractVersion == gglab::ShaderProcessContractVersion &&
							WireDiagnosticsCount(doc, diagnosticsCount) &&
								diagnosticsCount > 0;
					const bool noBusinessLeak = doc.has_value() &&
						!doc->contains("toolIdentity") &&
						!doc->contains("toolVersion") &&
						!doc->contains("producerKind") &&
						!doc->contains("producerIdentity") &&
						!doc->contains("supportedTargets");
					context.Check(
						run.m_ExitCode == 2 &&
							run.m_StdErr.empty() &&
							structuredFailure && noBusinessLeak,
						checkName.c_str());
				};
				CheckDescribeUsageError(positional, "extra positional argument");
				CheckDescribeUsageError(resultFormat, "--result-format json");
				CheckDescribeUsageError(bogusFlag, "unknown flag");
			}

			// Producer unavailable via the existing forced-unavailable seam.
			{
				const CliRunResult result =
					RunCli({ L"describe" }, /*forceCompilerUnavailable*/ true);
				const auto doc = ParseWireDocument(result.m_StdOut);
				std::string command;
				std::string status;
				bool success = true;
				std::int64_t exitCode = -1;
				std::int64_t contractVersion = -1;
				context.Check(
					result.m_ExitCode == 4 &&
						result.m_StdErr.empty() &&
						ReadWireString(doc, "command", command) &&
							command == "describe" &&
						ReadWireBoolean(doc, "success", success) && !success &&
						ReadWireString(doc, "status", status) &&
							status == "compiler-unavailable" &&
						ReadWireInteger(doc, "exitCode", exitCode) && exitCode == 4 &&
						ReadWireInteger(
							doc, "processContractVersion", contractVersion) &&
							contractVersion == gglab::ShaderProcessContractVersion &&
						doc.has_value() && !doc->contains("producerIdentity"),
					"describe reports compiler-unavailable (exit 4) when the producer is unavailable");
			}

			// Internal error floor via the describe internal-error seam.
			{
				const CliRunResult result = RunCli(
					{ L"describe" }, /*forceCompilerUnavailable*/ false,
					/*forceDescribeInternalError*/ true);
				const auto doc = ParseWireDocument(result.m_StdOut);
				std::string command;
				std::string status;
				bool success = true;
				std::int64_t exitCode = -1;
				std::int64_t contractVersion = -1;
				std::size_t diagnosticsCount = 0;
				context.Check(
					result.m_ExitCode == 7 &&
						result.m_StdErr.empty() &&
						ReadWireString(doc, "command", command) &&
							command == "describe" &&
						ReadWireBoolean(doc, "success", success) && !success &&
						ReadWireString(doc, "status", status) &&
							status == "internal-error" &&
						ReadWireInteger(doc, "exitCode", exitCode) && exitCode == 7 &&
						ReadWireInteger(
							doc, "processContractVersion", contractVersion) &&
							contractVersion == gglab::ShaderProcessContractVersion &&
						WireDiagnosticsCount(doc, diagnosticsCount) &&
							diagnosticsCount > 0,
					"describe reports internal-error (exit 7) on a forced handled failure");
			}
		}

		[[nodiscard]] bool WriteTextFile(
			const std::filesystem::path& path, std::string_view content) noexcept
		{
			std::filesystem::create_directories(path.parent_path());
			std::ofstream output(path, std::ios::binary);
			output.write(content.data(), static_cast<std::streamsize>(content.size()));
			return output.good();
		}

		[[nodiscard]] bool WriteBinaryFile(
			const std::filesystem::path& path, const ShaderBinary& binary) noexcept
		{
			std::filesystem::create_directories(path.parent_path());
			std::ofstream output(path, std::ios::binary);
			output.write(static_cast<const char*>(binary.Data()),
				static_cast<std::streamsize>(binary.SizeInBytes()));
			return output.good();
		}

		[[nodiscard]] std::optional<ShaderBinary> ReadFileBinary(
			const std::filesystem::path& path) noexcept
		{
			std::error_code errorCode;
			const auto fileSize = std::filesystem::file_size(path, errorCode);
			if (errorCode)
			{
				return std::nullopt;
			}
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				return std::nullopt;
			}
			ShaderBinary binary(static_cast<size_t>(fileSize));
			input.read(static_cast<char*>(binary.Data()), static_cast<std::streamsize>(fileSize));
			if (!input || input.gcount() != static_cast<std::streamsize>(fileSize))
			{
				return std::nullopt;
			}
			return binary;
		}

		[[nodiscard]] bool CliArtifactFieldsDescribeCommittedEntry(
			const CliRunResult& result) noexcept
		{
			if (result.m_ExitCode != 0 ||
				result.m_StdOut.find("\"manifestPath\"") != std::string::npos)
			{
				return false;
			}
			const std::string binaryHash = ExtractJsonField(result.m_StdOut, "binaryHash");
			const std::string binaryPathText = ExtractJsonField(result.m_StdOut, "binaryPath");
			const std::string recordPathText = ExtractJsonField(
				result.m_StdOut, "cacheRecordPath");
			if (binaryHash.empty() || binaryPathText.empty() || recordPathText.empty())
			{
				return false;
			}
			const std::filesystem::path binaryPath = utils::ToWideString(binaryPathText);
			const std::filesystem::path recordPath = utils::ToWideString(recordPathText);
			auto expectedRecordPath = binaryPath;
			expectedRecordPath += L".json";
			const std::optional<ShaderArtifactCacheRecord> committed =
				LoadShaderArtifactCacheRecord(recordPath, binaryPath);
			return recordPath.lexically_normal() == expectedRecordPath.lexically_normal() &&
				committed.has_value() &&
				Sha256DigestToHex(
					committed->m_Manifest.m_BinaryContentDigest.m_Digest) == binaryHash;
		}

		[[nodiscard]] bool CliRuntimeArtifactFieldsDescribePublishedEntry(
			const CliRunResult& result,
			const std::filesystem::path& artifactRoot,
			std::wstring_view expectedEntryPoint = L"VSMain") noexcept
		{
			const std::string artifactId = ExtractJsonField(result.m_StdOut, "artifactId");
			const std::filesystem::path runtimeBinaryPath = utils::ToWideString(
				ExtractJsonField(result.m_StdOut, "runtimeArtifactBinaryPath"));
			const std::filesystem::path runtimeManifestPath = utils::ToWideString(
				ExtractJsonField(result.m_StdOut, "runtimeArtifactManifestPath"));
			const std::filesystem::path cacheBinaryPath = utils::ToWideString(
				ExtractJsonField(result.m_StdOut, "binaryPath"));
			if (result.m_ExitCode != 0 || artifactId.empty() ||
				runtimeBinaryPath.empty() || runtimeManifestPath.empty() ||
				cacheBinaryPath.empty())
			{
				return false;
			}

			const std::optional<ShaderBinary> serializedManifest =
				ReadFileBinary(runtimeManifestPath);
			if (!serializedManifest.has_value())
			{
				return false;
			}
			const std::optional<ShaderRuntimeArtifactManifest> manifest =
				DeserializeShaderRuntimeArtifactManifest(std::span(
					static_cast<const std::byte*>(serializedManifest->Data()),
					serializedManifest->SizeInBytes()));
			if (!manifest.has_value() || Sha256DigestToHex(
				manifest->m_ArtifactId.m_DurableDigest) != artifactId ||
				manifest->m_EntryPoint != utils::ToString(expectedEntryPoint))
			{
				return false;
			}

			const ShaderArtifactRef artifactRef{ .m_ArtifactId = manifest->m_ArtifactId };
			const ShaderLooseArtifactLocator locator(artifactRoot);
			const ShaderLooseArtifactPaths expectedPaths = locator.GetPaths(artifactRef);
			ShaderLooseArtifactReader reader(locator);
			ShaderArtifactStore store(reader);
			const ShaderArtifactCompatibilityRequest compatibility{
				.m_TargetProfile = manifest->m_TargetProfile,
				.m_BinaryFormat = manifest->m_BinaryFormat,
				.m_SpirVTargetEnvironment = manifest->m_SpirVTargetEnvironment,
				.m_BindingABIRevision = manifest->m_BindingABIRevision,
				.m_CoordinateOptions = manifest->m_CoordinateOptions,
				.m_Stage = manifest->m_Stage,
			};
			const ShaderArtifactLoadResult loaded =
				store.LoadArtifact(artifactRef, compatibility);
			const std::optional<ShaderBinary> cacheBinary = ReadFileBinary(cacheBinaryPath);
			return runtimeBinaryPath.lexically_normal() ==
					expectedPaths.m_BinaryPath.lexically_normal() &&
				runtimeManifestPath.lexically_normal() ==
					expectedPaths.m_ManifestPath.lexically_normal() &&
				loaded.IsSuccess() && cacheBinary.has_value() &&
				loaded.m_Artifact.m_Binary.SizeInBytes() == cacheBinary->SizeInBytes() &&
				std::memcmp(
					loaded.m_Artifact.m_Binary.Data(),
					cacheBinary->Data(),
					cacheBinary->SizeInBytes()) == 0;
		}

		struct RuntimeCompileEvidence
		{
			std::string m_RecipeId;
			std::string m_BuildKey;
			std::string m_BinaryDigest;
			ShaderBinary m_Binary;
			bool m_Succeeded = false;
		};

		[[nodiscard]] RuntimeCompileEvidence CompileWithRuntime(const std::filesystem::path& sourceRoot,
			const std::filesystem::path& cacheRoot, std::wstring_view logicalSource,
			ShaderStage stage, std::wstring_view entry, ShaderTargetProfile targetProfile) noexcept
		{
			RuntimeCompileEvidence evidence{};
			ShaderCompiler compiler(sourceRoot, cacheRoot);
			ShaderDesc desc{};
			desc.m_SourcePath = logicalSource;
			desc.m_Stage = stage;
			ShaderCompileTarget compileTarget = MakeDX12CompileTarget(stage);
			if (targetProfile == ShaderTargetProfile::GGLabVulkan13)
			{
				compileTarget = MakeVulkan13CompileTarget(stage);
			}
			desc.m_Target = compileTarget;
			desc.m_Target.m_Flags = ShaderCompileFlags::Optimization;
			desc.m_Entry = entry;
			desc.m_IncludeDirs = { L"." };

			const ShaderResolvedRecipe recipe = compiler.Resolve(desc);
			if (!recipe.IsSuccess())
			{
				return evidence;
			}
			const ShaderCompileResult result = compiler.CompileOrLoad(recipe);
			if (!result.IsSuccess())
			{
				return evidence;
			}
			evidence.m_RecipeId = Sha256DigestToHex(recipe.m_RecipeId.m_DurableDigest);
			evidence.m_BuildKey = Sha256DigestToHex(recipe.m_BuildKey.m_DurableDigest);
			evidence.m_BinaryDigest = Sha256DigestToHex(
				result.m_Artifact.m_Manifest.m_BinaryContentDigest.m_Digest);
			evidence.m_Binary = result.m_Artifact.m_Binary;
			evidence.m_Succeeded = true;
			return evidence;
		}

		struct ParityCase
		{
			std::wstring_view m_Source;
			ShaderStage m_Stage;
			std::wstring_view m_Entry;
			std::string_view m_StageName;
			ShaderTargetProfile m_Profile;
			std::string_view m_TargetName;
		};

		void RunParityTests(SelfTestContext& context, const std::filesystem::path& sourceRoot,
			const std::filesystem::path& tempRoot) noexcept
		{
			constexpr std::array ParityCases{
				ParityCase{ L"Passes/PassForwardCoverage.hlsl", ShaderStage::Vertex, L"VSMain",
					"vertex", ShaderTargetProfile::GGLabDX12, "gglab-dx12" },
				ParityCase{ L"Passes/PassForwardPBR.hlsl", ShaderStage::Pixel, L"PSMain",
					"pixel", ShaderTargetProfile::GGLabDX12, "gglab-dx12" },
				ParityCase{ L"Passes/PassForwardPlusCull.hlsl", ShaderStage::Compute, L"CSMain",
					"compute", ShaderTargetProfile::GGLabDX12, "gglab-dx12" },
				ParityCase{ L"Passes/PassForwardCoverage.hlsl", ShaderStage::Vertex, L"VSMain",
					"vertex", ShaderTargetProfile::GGLabVulkan13, "gglab-vulkan13" },
				ParityCase{ L"Passes/PassForwardPBR.hlsl", ShaderStage::Pixel, L"PSMain",
					"pixel", ShaderTargetProfile::GGLabVulkan13, "gglab-vulkan13" },
				ParityCase{ L"Passes/PassForwardPlusCull.hlsl", ShaderStage::Compute, L"CSMain",
					"compute", ShaderTargetProfile::GGLabVulkan13, "gglab-vulkan13" },
			};

			bool allParityCasesMatch = true;
			const std::filesystem::path artifactRoot = tempRoot / L"CliRuntimeArtifacts";
			for (const ParityCase& parityCase : ParityCases)
			{
				const std::filesystem::path runtimeCache = tempRoot / L"RuntimeParityCache";
				const std::filesystem::path cliCache = tempRoot / L"CliParityCache";
				const RuntimeCompileEvidence runtimeEvidence = CompileWithRuntime(sourceRoot,
					runtimeCache, parityCase.m_Source, parityCase.m_Stage, parityCase.m_Entry,
					parityCase.m_Profile);
				if (!runtimeEvidence.m_Succeeded)
				{
					allParityCasesMatch = false;
					continue;
				}

				const CliRunResult cliResult = RunCli({
					L"compile",
					L"--source-root", sourceRoot.wstring(),
					L"--source", std::wstring(parityCase.m_Source),
					L"--stage", utils::ToWideString(parityCase.m_StageName),
					L"--entry", std::wstring(parityCase.m_Entry),
					L"--target", utils::ToWideString(parityCase.m_TargetName),
					L"--include", L".",
					L"--cache-root", cliCache.wstring(),
					L"--artifact-root", artifactRoot.wstring(),
					L"--result-format", L"json",
				});
				if (cliResult.m_ExitCode != 0)
				{
					allParityCasesMatch = false;
					continue;
				}

				const std::string recipeId = ExtractJsonField(cliResult.m_StdOut, "recipeId");
				const std::string buildKey = ExtractJsonField(cliResult.m_StdOut, "buildKey");
				const std::string binaryDigest = ExtractJsonField(cliResult.m_StdOut, "binaryHash");
				const std::string binaryPath = ExtractJsonField(cliResult.m_StdOut, "binaryPath");
				const std::optional<ShaderBinary> cliBinary = ReadFileBinary(
					utils::ToWideString(binaryPath));
				allParityCasesMatch &= recipeId == runtimeEvidence.m_RecipeId &&
					buildKey == runtimeEvidence.m_BuildKey &&
					binaryDigest == runtimeEvidence.m_BinaryDigest &&
					CliArtifactFieldsDescribeCommittedEntry(cliResult) &&
					CliRuntimeArtifactFieldsDescribePublishedEntry(
						cliResult, artifactRoot, parityCase.m_Entry) &&
					cliBinary.has_value() && cliBinary->SizeInBytes() ==
						runtimeEvidence.m_Binary.SizeInBytes() &&
					(cliBinary->SizeInBytes() == 0 ||
						std::memcmp(cliBinary->Data(), runtimeEvidence.m_Binary.Data(),
							cliBinary->SizeInBytes()) == 0);
			}
			context.Check(allParityCasesMatch,
				"gglab-shaderc and Runtime Store agree on identity, manifest, and exact bytes for VS/PS/CS across both targets");
		}

		void RunCliBehaviorTests(SelfTestContext& context, const std::filesystem::path& sourceRoot,
			const std::filesystem::path& tempRoot) noexcept
		{
			// Cache reuse: the second run reports a hit.
			const std::filesystem::path cacheRoot = tempRoot / L"CliBehaviorCache";
			const CliRunResult firstRun = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--entry", L"VSMain",
				L"--target", L"gglab-vulkan13", L"--include", L".", L"--cache-root",
				cacheRoot.wstring(),
			});
			const CliRunResult secondRun = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--entry", L"VSMain",
				L"--target", L"gglab-vulkan13", L"--include", L".", L"--cache-root",
				cacheRoot.wstring(),
			});
			context.Check(firstRun.m_ExitCode == 0 && secondRun.m_ExitCode == 0 &&
				firstRun.m_StdOut.find("Cache: miss") != std::string::npos &&
				secondRun.m_StdOut.find("Cache: hit") != std::string::npos &&
				firstRun.m_StdOut.find("Cache record: ") != std::string::npos &&
				firstRun.m_StdOut.find("Manifest: ") == std::string::npos,
				"CLI text output reports cache miss then hit for the same request");

			const std::filesystem::path jsonContractCache = tempRoot / L"CliJsonContractCache";
			const std::vector<std::wstring> jsonContractArguments{
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--entry", L"VSMain",
				L"--target", L"gglab-dx12", L"--include", L".", L"--cache-root",
				jsonContractCache.wstring(), L"--result-format", L"json",
			};
			const CliRunResult jsonPublished = RunCli(jsonContractArguments);
			const CliRunResult jsonHit = RunCli(jsonContractArguments);
			context.Check(CliArtifactFieldsDescribeCommittedEntry(jsonPublished) &&
				CliArtifactFieldsDescribeCommittedEntry(jsonHit) &&
				jsonPublished.m_StdOut.find("\"fromCache\":false") != std::string::npos &&
				jsonHit.m_StdOut.find("\"fromCache\":true") != std::string::npos &&
				jsonPublished.m_StdOut.find("\"cacheRecordPath\":\"") != std::string::npos &&
				jsonHit.m_StdOut.find("\"cacheRecordPath\":\"") != std::string::npos,
				"CLI binaryHash, binaryPath, and cacheRecordPath describe one committed artifact on publish and hit paths");

			// Deterministic CommittedByOther CLI handoff. Install a structurally valid
			// same-slot winner with the current recipe/build key and dependency
			// provenance, but with a different valid DXIL payload and producer metadata.
			// The changed compiler identity rejects the initial cache-hit path. Keeping
			// the committed binary open without delete sharing prevents the child CLI
			// from replacing it, so final observation must classify the preserved,
			// non-equivalent entry as CommittedByOther.
			const std::filesystem::path committedByOtherCache =
				tempRoot / L"CliCommittedByOtherCache";
			ShaderCompiler winnerCompiler(sourceRoot, committedByOtherCache);
			ShaderDesc winnerDesc{};
			winnerDesc.m_SourcePath = L"Passes/PassForwardCoverage.hlsl";
			winnerDesc.m_Stage = ShaderStage::Vertex;
			winnerDesc.m_Target = MakeDX12CompileTarget(ShaderStage::Vertex);
			winnerDesc.m_Target.m_Flags = ShaderCompileFlags::Optimization;
			winnerDesc.m_Entry = L"VSMain";
			winnerDesc.m_IncludeDirs = { L"." };
			const ShaderResolvedRecipe winnerRecipe = winnerCompiler.Resolve(winnerDesc);
			const ShaderCompileResult winnerBaseline =
				winnerCompiler.CompileOrLoad(winnerRecipe);
			const std::filesystem::path winnerBinaryPath =
				winnerCompiler.GetCacheBinaryPath(winnerRecipe);
			auto winnerRecordPath = winnerBinaryPath;
			winnerRecordPath += L".json";
			const std::optional<ShaderArtifactCacheRecord> baselineRecord =
				LoadShaderArtifactCacheRecord(winnerRecordPath, winnerBinaryPath);

			ShaderCompiler variantCompiler(sourceRoot, tempRoot / L"CliCommittedByOtherVariant");
			ShaderDesc variantDesc = winnerDesc;
			variantDesc.m_Target.m_Flags = ShaderCompileFlags::Debug;
			const ShaderResolvedRecipe variantRecipe = variantCompiler.Resolve(variantDesc);
			const ShaderCompileResult variantResult = variantCompiler.CompileOrLoad(variantRecipe);

			bool competingWinnerInstalled = false;
			ShaderArtifactCacheRecord competingWinner{};
			if (winnerRecipe.IsSuccess() && winnerBaseline.IsSuccess() &&
				baselineRecord.has_value() && variantRecipe.IsSuccess() &&
				variantResult.IsSuccess() && variantResult.m_Artifact.m_Binary.IsValid())
			{
				competingWinner = *baselineRecord;
				competingWinner.m_Manifest.m_CompilerIdentity.m_CanonicalIdentity +=
					L"+cli-committed-winner";
				competingWinner.m_Manifest.m_BinaryContentDigest =
					variantResult.m_Artifact.m_Manifest.m_BinaryContentDigest;
				competingWinner.m_Binary = variantResult.m_Artifact.m_Binary;
				competingWinnerInstalled =
					competingWinner.m_Manifest.m_RecipeId == winnerRecipe.m_RecipeId &&
					competingWinner.m_Manifest.m_BuildKey == winnerRecipe.m_BuildKey &&
					competingWinner.m_Manifest.m_CompilerIdentity.m_CanonicalIdentity !=
						winnerRecipe.m_CompilerIdentity.m_CanonicalIdentity &&
					competingWinner.m_Manifest.m_Dependencies ==
						baselineRecord->m_Manifest.m_Dependencies &&
					competingWinner.m_Manifest.m_Dependencies ==
						variantResult.m_Artifact.m_Manifest.m_Dependencies &&
					competingWinner.m_Manifest.m_BinaryContentDigest !=
						baselineRecord->m_Manifest.m_BinaryContentDigest &&
					WriteBinaryFile(winnerBinaryPath, competingWinner.m_Binary) &&
					WriteShaderArtifactCacheRecord(winnerRecordPath, competingWinner);
			}

			const HANDLE winnerBinaryGuard = CreateFileW(winnerBinaryPath.c_str(), GENERIC_READ,
				FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			const CliRunResult committedByOther = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--entry", L"VSMain",
				L"--target", L"gglab-dx12", L"--include", L".", L"--cache-root",
				committedByOtherCache.wstring(), L"--result-format", L"json",
			});
			if (winnerBinaryGuard != INVALID_HANDLE_VALUE)
			{
				CloseHandle(winnerBinaryGuard);
			}

			const std::optional<ShaderArtifactCacheRecord> observedWinner =
				LoadShaderArtifactCacheRecord(winnerRecordPath, winnerBinaryPath);
			const std::string committedByOtherHash =
				ExtractJsonField(committedByOther.m_StdOut, "binaryHash");
			const std::string expectedWinnerHash = Sha256DigestToHex(
				competingWinner.m_Manifest.m_BinaryContentDigest.m_Digest);
			const std::filesystem::path cliWinnerBinaryPath = utils::ToWideString(
				ExtractJsonField(committedByOther.m_StdOut, "binaryPath"));
			const std::filesystem::path cliWinnerRecordPath = utils::ToWideString(
				ExtractJsonField(committedByOther.m_StdOut, "cacheRecordPath"));
			context.Check(competingWinnerInstalled && winnerBinaryGuard != INVALID_HANDLE_VALUE &&
				committedByOther.m_ExitCode == 0 &&
				committedByOther.m_StdOut.find("\"fromCache\":true") != std::string::npos &&
				committedByOtherHash == expectedWinnerHash &&
				cliWinnerBinaryPath.lexically_normal() == winnerBinaryPath.lexically_normal() &&
				cliWinnerRecordPath.lexically_normal() == winnerRecordPath.lexically_normal() &&
				observedWinner.has_value() && observedWinner->m_Manifest == competingWinner.m_Manifest &&
				observedWinner->m_Binary.SizeInBytes() == competingWinner.m_Binary.SizeInBytes() &&
				std::memcmp(observedWinner->m_Binary.Data(), competingWinner.m_Binary.Data(),
					competingWinner.m_Binary.SizeInBytes()) == 0 &&
				CliArtifactFieldsDescribeCommittedEntry(committedByOther),
				"CLI CommittedByOther success fields describe the delivered committed winner");

			// Corrupt cache binary data is rebuilt, never fatal.
			{
				const CliRunResult probeRun = RunCli({
					L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
					L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--entry",
					L"VSMain", L"--target", L"gglab-dx12", L"--include", L".",
					L"--cache-root", cacheRoot.wstring(), L"--result-format", L"json",
				});
				const std::string binaryPath = ExtractJsonField(probeRun.m_StdOut, "binaryPath");
				const std::filesystem::path corruptedPath = utils::ToWideString(binaryPath);
				const bool corrupted = WriteTextFile(corruptedPath, "corrupted derived data");
				const CliRunResult recoveredRun = RunCli({
					L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
					L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--entry",
					L"VSMain", L"--target", L"gglab-dx12", L"--include", L".",
					L"--cache-root", cacheRoot.wstring(),
				});
				context.Check(corrupted && recoveredRun.m_ExitCode == 0 &&
					recoveredRun.m_StdOut.find("Cache: miss") != std::string::npos,
					"CLI treats corrupt cached binary data as a cache miss and rebuilds");
			}

			// Failure matrix.
			const CliRunResult unknownTarget = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--target",
				L"gglab-ps5",
			});
			const CliRunResult unknownStage = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"raygen", L"--target",
				L"gglab-dx12",
			});
			const CliRunResult missingSourceRoot = RunCli({
				L"compile", L"--source", L"Passes/PassForwardCoverage.hlsl", L"--stage",
				L"vertex", L"--target", L"gglab-dx12",
			});
			const CliRunResult missingSource = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassDoesNotExist.hlsl", L"--stage", L"vertex", L"--target", L"gglab-dx12",
			});
			context.Check(unknownTarget.m_ExitCode == 2 && unknownStage.m_ExitCode == 2 &&
				missingSourceRoot.m_ExitCode == 2 && missingSource.m_ExitCode == 3,
				"CLI exit codes distinguish command-line errors from invalid shader requests");

			// DXC syntax error maps to exit code 4.
			const std::filesystem::path badSourceRoot = tempRoot / L"BadCliSources";
			const bool badSourceWritten =
				WriteTextFile(badSourceRoot / L"Bad.hlsl", "this is not valid hlsl");
			const CliRunResult syntaxError = RunCli({
				L"compile", L"--source-root", badSourceRoot.wstring(), L"--source", L"Bad.hlsl",
				L"--stage", L"compute", L"--entry", L"CSMain", L"--target", L"gglab-dx12",
				L"--cache-root", (tempRoot / L"BadCliCache").wstring(),
			});
			context.Check(badSourceWritten && syntaxError.m_ExitCode == 4 &&
				!syntaxError.m_StdErr.empty(),
				"CLI reports DXC syntax errors as compilation failures with diagnostics");

			// Artifact IO failure (cache root under a regular file) maps to exit 5.
			const std::filesystem::path fileBlock = tempRoot / L"NotADirectory.txt";
			const bool fileBlockWritten = WriteTextFile(fileBlock, "block");
			const CliRunResult ioFailure = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--target",
				L"gglab-dx12", L"--include", L".", L"--cache-root",
				(fileBlock / L"Cache").wstring(),
			});
			context.Check(fileBlockWritten && ioFailure.m_ExitCode == 5,
				"CLI maps artifact IO failures to the dedicated exit code");

			const CliRunResult publicationIoFailure = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--target",
				L"gglab-dx12", L"--include", L".", L"--cache-root",
				(tempRoot / L"PublicationIoFailureCache").wstring(), L"--artifact-root",
				(fileBlock / L"Artifacts").wstring(),
			});
			context.Check(fileBlockWritten && publicationIoFailure.m_ExitCode == 5,
				"CLI maps runtime artifact publication IO failures to the dedicated exit code");

			// Windows path robustness: source root with spaces and Unicode, plus a
			// self-contained shader without includes.
			const std::filesystem::path exoticRoot = tempRoot / L"Sources With Spaces 着色器";
			const bool exoticSourceWritten = WriteTextFile(exoticRoot / L"SelfContained.hlsl",
				"float4 VSMain(uint id : SV_VertexID) : SV_Position { return float4(0, 0, 0, 1); }");
			const CliRunResult exoticDx12 = RunCli({
				L"compile", L"--source-root", exoticRoot.wstring(), L"--source",
				L"SelfContained.hlsl", L"--stage", L"vertex", L"--target", L"gglab-dx12",
				L"--cache-root", (tempRoot / L"ExoticCache").wstring(),
			});
			const CliRunResult exoticVulkan = RunCli({
				L"compile", L"--source-root", exoticRoot.wstring(), L"--source",
				L"SelfContained.hlsl", L"--stage", L"vertex", L"--target", L"gglab-vulkan13",
				L"--cache-root", (tempRoot / L"ExoticCache").wstring(), L"--result-format", L"json",
			});
			const bool exoticPathsCompiled = exoticSourceWritten && exoticDx12.m_ExitCode == 0 &&
				exoticVulkan.m_ExitCode == 0 &&
				exoticVulkan.m_StdOut.find("\"success\":true") != std::string::npos;
			const std::string exoticPathDiagnostics = exoticPathsCompiled
				? "CLI compiles through source roots containing spaces and Unicode"
				: std::format(
					"CLI compiles through source roots containing spaces and Unicode "
					"[sourceWritten={}, dx12Exit={}, dx12Out=\"{}\", dx12Err=\"{}\", "
					"vulkanExit={}, vulkanOut=\"{}\", vulkanErr=\"{}\"]",
					exoticSourceWritten, exoticDx12.m_ExitCode, exoticDx12.m_StdOut,
					exoticDx12.m_StdErr, exoticVulkan.m_ExitCode, exoticVulkan.m_StdOut,
					exoticVulkan.m_StdErr);
			context.Check(exoticPathsCompiled,
				exoticPathDiagnostics.c_str());

			// Observability commands.
			const CliRunResult targets = RunCli({ L"targets" });
			const CliRunResult version = RunCli({ L"--version" });
			context.Check(targets.m_ExitCode == 0 &&
				targets.m_StdOut.find("gglab-dx12") != std::string::npos &&
				targets.m_StdOut.find("gglab-vulkan13") != std::string::npos &&
				version.m_ExitCode == 0 &&
				version.m_StdOut.find("Producer: dxc") != std::string::npos &&
				version.m_StdOut.find("unknown") == std::string::npos,
				"CLI targets and --version report profiles and the concrete producer identity");
		}

		void RunJsonProcessContractTests(SelfTestContext& context,
			const std::filesystem::path& sourceRoot,
			const std::filesystem::path& tempRoot) noexcept
		{
			const std::filesystem::path matrixCache = tempRoot / L"JsonProcessContractCache";
			const std::vector<std::wstring> successArguments{
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--entry", L"VSMain",
				L"--target", L"gglab-dx12", L"--include", L".", L"--cache-root",
				matrixCache.wstring(), L"--result-format", L"json",
			};
			const CliRunResult success = RunCli(successArguments);
			const CliRunResult unknownCommand = RunCli({
				L"unknown-command", L"--result-format", L"json",
			});
			const CliRunResult unknownOption = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--target",
				L"gglab-dx12", L"--unknown-option", L"--result-format", L"json",
			});
			const CliRunResult missingRequiredOption = RunCli({
				L"compile", L"--source", L"Passes/PassForwardCoverage.hlsl", L"--stage",
				L"vertex", L"--target", L"gglab-dx12", L"--result-format", L"json",
			});
			const CliRunResult unknownStage = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"raygen", L"--target",
				L"gglab-dx12", L"--result-format", L"json",
			});
			const CliRunResult unknownTarget = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--target",
				L"gglab-ps5", L"--result-format", L"json",
			});
			const CliRunResult invalidRequest = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source", L"../Outside.hlsl",
				L"--stage", L"vertex", L"--target", L"gglab-dx12",
				L"--result-format", L"json",
			});
			const CliRunResult missingSource = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassDoesNotExist.hlsl", L"--stage", L"vertex", L"--target",
				L"gglab-dx12", L"--result-format", L"json",
			});
			const CliRunResult compilerUnavailable = RunCli(successArguments, true);

			const std::filesystem::path badSourceRoot = tempRoot / L"JsonMatrixBadSources";
			const bool badSourceWritten =
				WriteTextFile(badSourceRoot / L"Bad.hlsl", "this is not valid hlsl");
			const CliRunResult compileFailure = RunCli({
				L"compile", L"--source-root", badSourceRoot.wstring(), L"--source", L"Bad.hlsl",
				L"--stage", L"compute", L"--entry", L"CSMain", L"--target", L"gglab-dx12",
				L"--cache-root", (tempRoot / L"JsonMatrixBadCache").wstring(),
				L"--result-format", L"json",
			});

			const std::filesystem::path fileBlock = tempRoot / L"JsonMatrixNotADirectory.txt";
			const bool fileBlockWritten = WriteTextFile(fileBlock, "block");
			const CliRunResult artifactIoFailure = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--target",
				L"gglab-dx12", L"--include", L".", L"--cache-root",
				(fileBlock / L"Cache").wstring(), L"--result-format", L"json",
			});

			// A stale committed dependency record is readable but cannot be
			// overwritten while its binary is held without delete sharing. After
			// the include changes, both publication attempts observe the stale
			// provenance and deterministically produce SourceChangedDuringCompile.
			const std::filesystem::path sourceChangedRoot = tempRoot / L"JsonSourceChangedSources";
			const std::filesystem::path sourceChangedCache = tempRoot / L"JsonSourceChangedCache";
			const bool sourceChangedFilesWritten =
				WriteTextFile(sourceChangedRoot / L"Probe.hlsli",
					"static const float ProbeValue = 0.25f;\n") &&
				WriteTextFile(sourceChangedRoot / L"Main.hlsl",
					"#include \"Probe.hlsli\"\n"
					"[numthreads(1, 1, 1)]\n"
					"void CSMain(uint3 id : SV_DispatchThreadID) { float value = ProbeValue + id.x; }\n");
			const std::vector<std::wstring> sourceChangedArguments{
				L"compile", L"--source-root", sourceChangedRoot.wstring(), L"--source", L"Main.hlsl",
				L"--stage", L"compute", L"--entry", L"CSMain", L"--target", L"gglab-dx12",
				L"--include", L".", L"--cache-root", sourceChangedCache.wstring(),
				L"--result-format", L"json",
			};
			const CliRunResult sourceChangedBaseline = RunCli(sourceChangedArguments);
			const std::filesystem::path sourceChangedBinaryPath = utils::ToWideString(
				ExtractJsonField(sourceChangedBaseline.m_StdOut, "binaryPath"));
			const bool dependencyChanged = WriteTextFile(sourceChangedRoot / L"Probe.hlsli",
				"static const float ProbeValue = 0.75f;\n");
			const HANDLE sourceChangedBinaryGuard = CreateFileW(sourceChangedBinaryPath.c_str(),
				GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			const CliRunResult sourceChanged = RunCli(sourceChangedArguments);
			if (sourceChangedBinaryGuard != INVALID_HANDLE_VALUE)
			{
				CloseHandle(sourceChangedBinaryGuard);
			}

			const CliRunResult invalidResultFormat = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--target",
				L"gglab-dx12", L"--result-format", L"xml",
			});
			const CliRunResult duplicateResultFormat = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--target",
				L"gglab-dx12", L"--result-format", L"json", L"--result-format", L"xml",
			});

			const bool allOutcomesStructured =
				HasJsonEnvelope(success, "ok", 0, true) &&
				HasJsonEnvelope(unknownCommand, "usage-error", 2, false) &&
				HasJsonEnvelope(unknownOption, "usage-error", 2, false) &&
				HasJsonEnvelope(missingRequiredOption, "usage-error", 2, false) &&
				HasJsonEnvelope(unknownStage, "usage-error", 2, false) &&
				HasJsonEnvelope(unknownTarget, "usage-error", 2, false) &&
				HasJsonEnvelope(invalidRequest, "invalid-request", 3, false) &&
				HasJsonEnvelope(missingSource, "source-not-found", 3, false) &&
				HasJsonEnvelope(compilerUnavailable, "compiler-unavailable", 4, false) &&
				badSourceWritten && HasJsonEnvelope(compileFailure, "compile-failed", 4, false) &&
				fileBlockWritten &&
					HasJsonEnvelope(artifactIoFailure, "artifact-io-failure", 5, false) &&
				sourceChangedFilesWritten && dependencyChanged &&
					sourceChangedBinaryGuard != INVALID_HANDLE_VALUE &&
					HasJsonEnvelope(sourceChanged, "source-changed", 6, false);
			context.Check(allOutcomesStructured,
				"CLI JSON mode emits one stdout document and empty stderr for all 12 outcomes");

			context.Check(invalidResultFormat.m_ExitCode == 2 &&
				invalidResultFormat.m_StdOut.empty() &&
				invalidResultFormat.m_StdErr.find("Invalid --result-format value: xml") !=
					std::string::npos &&
				HasJsonEnvelope(duplicateResultFormat, "usage-error", 2, false) &&
				ExtractJsonField(duplicateResultFormat.m_StdOut, "message") ==
					"--result-format specified multiple times",
				"Result-format pre-scan resolves invalid and duplicate mode selection without ambiguity");
		}

		void RunCrossProcessHardGateTests(SelfTestContext& context,
			const std::filesystem::path& sourceRoot,
			const std::filesystem::path& tempRoot) noexcept
		{
			// Cross-process hard gate: two gglab-shaderc processes race the
			// same cold cache slot concurrently. Both must complete their
			// publication, and the slot's committed state must be a consistent
			// cache hit afterwards. Rounds use distinct defines so every round
			// races a fresh cold slot instead of reusing a previous winner.
			const std::filesystem::path gateCache = tempRoot / L"CrossProcessGateCache";
			const std::filesystem::path gateArtifacts = tempRoot / L"CrossProcessArtifacts";
			constexpr int GateRoundCount = 3;
			bool allWorkersSucceeded = true;
			bool allThirdRunsHitCache = true;
			bool allHashesConverged = true;
			bool allArtifactIdsConverged = true;
			bool allArtifactFieldsConsistent = true;
			std::string workerDiagnostics;
			for (int round = 0; round < GateRoundCount; ++round)
			{
				const std::vector<std::wstring> arguments{
					L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
					L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--entry", L"VSMain",
					L"--target", L"gglab-dx12", L"--include", L".", L"--cache-root",
					gateCache.wstring(), L"--artifact-root", gateArtifacts.wstring(),
					L"--result-format", L"json", L"--define",
					std::format(L"GGLAB_CROSS_PROCESS_GATE={}", round),
				};

				CliRunResult first{};
				CliRunResult second{};
				std::thread firstWorker([&]() noexcept { first = RunCli(arguments); });
				std::thread secondWorker([&]() noexcept { second = RunCli(arguments); });
				firstWorker.join();
				secondWorker.join();

				const CliRunResult thirdRun = RunCli(arguments);
				const std::string firstHash = ExtractJsonField(first.m_StdOut, "binaryHash");
				const std::string secondHash = ExtractJsonField(second.m_StdOut, "binaryHash");
				const std::string thirdHash = ExtractJsonField(thirdRun.m_StdOut, "binaryHash");
				const std::string firstArtifactId =
					ExtractJsonField(first.m_StdOut, "artifactId");
				const std::string secondArtifactId =
					ExtractJsonField(second.m_StdOut, "artifactId");
				const std::string thirdArtifactId =
					ExtractJsonField(thirdRun.m_StdOut, "artifactId");
				allWorkersSucceeded &= first.m_ExitCode == 0 && second.m_ExitCode == 0;
				allThirdRunsHitCache &= thirdRun.m_ExitCode == 0 &&
					thirdRun.m_StdOut.find("\"fromCache\":true") != std::string::npos;
				allHashesConverged &= !firstHash.empty() && firstHash == secondHash &&
					secondHash == thirdHash;
				allArtifactIdsConverged &= !firstArtifactId.empty() &&
					firstArtifactId == secondArtifactId &&
					secondArtifactId == thirdArtifactId;
				allArtifactFieldsConsistent &=
					CliArtifactFieldsDescribeCommittedEntry(first) &&
					CliArtifactFieldsDescribeCommittedEntry(second) &&
					CliArtifactFieldsDescribeCommittedEntry(thirdRun) &&
					CliRuntimeArtifactFieldsDescribePublishedEntry(first, gateArtifacts) &&
					CliRuntimeArtifactFieldsDescribePublishedEntry(second, gateArtifacts) &&
					CliRuntimeArtifactFieldsDescribePublishedEntry(thirdRun, gateArtifacts);
				if (first.m_ExitCode != 0 || second.m_ExitCode != 0 || thirdRun.m_ExitCode != 0)
				{
					const auto AppendProcessDiagnostics = [&workerDiagnostics](int round,
						std::string_view tag, const CliRunResult& result) noexcept
						{
							workerDiagnostics += std::format(
								" [round{}-{}: exit={} stdout=\"{}\" stderr=\"{}\"]",
								round, tag, result.m_ExitCode, result.m_StdOut, result.m_StdErr);
						};
					if (first.m_ExitCode != 0)
					{
						AppendProcessDiagnostics(round, "A", first);
					}
					if (second.m_ExitCode != 0)
					{
						AppendProcessDiagnostics(round, "B", second);
					}
					if (thirdRun.m_ExitCode != 0)
					{
						AppendProcessDiagnostics(round, "reload", thirdRun);
					}
				}
			}
			context.Check(allWorkersSucceeded,
				("Both concurrent gglab-shaderc processes complete their publication"
					+ workerDiagnostics).c_str());
			context.Check(allThirdRunsHitCache,
				"Post-race reload hits the committed cross-process cache entry");
			context.Check(allHashesConverged,
				"Concurrent processes and the post-race reload agree on one committed binary digest");
			context.Check(allArtifactIdsConverged,
				"Concurrent CLI publishers converge on one immutable Runtime ArtifactId");
			context.Check(allArtifactFieldsConsistent,
				"Concurrent CLI fields resolve to Store-valid cache and Runtime artifacts");
		}

		void RunRuntimeBuildContractTests(SelfTestContext& context,
			const std::filesystem::path& sourceRoot,
			const std::filesystem::path& tempRoot) noexcept
		{
			const CliRunResult missingRequiredOption = RunCli({
				L"build-runtime", L"--result-format", L"json",
			});
			const CliRunResult unknownOption = RunCli({
				L"build-runtime", L"--bogus", L"--result-format", L"json",
			});
			const CliRunResult duplicateResultFormat = RunCli({
				L"build-runtime", L"--result-format", L"json",
				L"--result-format", L"json",
			});
			context.Check(
				HasJsonEnvelope(missingRequiredOption, "usage-error", 2, false,
					"build-runtime") &&
				HasJsonEnvelope(unknownOption, "usage-error", 2, false,
					"build-runtime") &&
				HasJsonEnvelope(duplicateResultFormat, "usage-error", 2, false,
					"build-runtime"),
				"build-runtime JSON usage errors truthfully identify their command");

			const std::filesystem::path cacheRoot = tempRoot / L"RuntimeBuildCache";
			const std::filesystem::path artifactRoot = tempRoot / L"RuntimeBuildArtifacts";
			const std::vector<std::wstring> arguments{
				L"build-runtime",
				L"--source-root", sourceRoot.wstring(),
				L"--target", L"gglab-dx12",
				L"--cache-root", cacheRoot.wstring(),
				L"--artifact-root", artifactRoot.wstring(),
				L"--result-format", L"json",
			};

			const CliRunResult first = RunCli(arguments);
			const std::string firstRegistryId = ExtractJsonField(first.m_StdOut, "registryId");
			ShaderLooseActiveProgramRegistryReader activeReader{
				ShaderLooseActiveProgramRegistryLocator(
					artifactRoot, ShaderTargetProfile::GGLabDX12)
			};
			const ActiveShaderProgramRegistryReadResult firstActive = activeReader.Read();
			ShaderLooseProgramRegistryArtifactReader registryReader{
				ShaderLooseProgramRegistryArtifactLocator(artifactRoot)
			};
			const ShaderProgramRegistryArtifactReadResult firstRegistry = firstActive.IsSuccess()
				? registryReader.ReadArtifact(firstActive.m_RegistryRef)
				: ShaderProgramRegistryArtifactReadResult{};
			context.Check(
				first.m_ExitCode == 0 && IsSingleJsonDocument(first) &&
					first.m_StdOut.find("\"command\":\"build-runtime\"") != std::string::npos &&
					first.m_StdOut.find("\"success\":true") != std::string::npos &&
					first.m_StdOut.find("\"programCount\":53") != std::string::npos &&
					firstRegistryId.size() == 64 && firstActive.IsSuccess() &&
					Sha256DigestToHex(
						firstActive.m_RegistryRef.m_RegistryId.m_DurableDigest) ==
							firstRegistryId && firstRegistry.IsSuccess() &&
					firstRegistry.m_Artifact.m_Entries.size() == 53,
				"build-runtime publishes the complete immutable catalog and active RegistryId");

			const CliRunResult second = RunCli(arguments);
			const ActiveShaderProgramRegistryReadResult secondActive = activeReader.Read();
			context.Check(
				second.m_ExitCode == 0 && ExtractJsonField(second.m_StdOut, "registryId") ==
					firstRegistryId && secondActive.IsSuccess() &&
					secondActive.m_RegistryRef == firstActive.m_RegistryRef,
				"Repeated build-runtime invocation reuses the same immutable active snapshot");

			const CliRunResult unavailable = RunCli(arguments, true);
			const ActiveShaderProgramRegistryReadResult afterFailure = activeReader.Read();
			context.Check(
				unavailable.m_ExitCode == 4 && IsSingleJsonDocument(unavailable) &&
					unavailable.m_StdOut.find("\"success\":false") != std::string::npos &&
					afterFailure.IsSuccess() &&
					afterFailure.m_RegistryRef == firstActive.m_RegistryRef,
				"Failed external runtime build preserves the last-known-good active snapshot");

			CliRunResult concurrentFirst{};
			CliRunResult concurrentSecond{};
			std::thread firstWorker([&]() noexcept { concurrentFirst = RunCli(arguments); });
			std::thread secondWorker([&]() noexcept { concurrentSecond = RunCli(arguments); });
			firstWorker.join();
			secondWorker.join();
			const ActiveShaderProgramRegistryReadResult concurrentActive = activeReader.Read();
			context.Check(
				concurrentFirst.m_ExitCode == 0 && concurrentSecond.m_ExitCode == 0 &&
					ExtractJsonField(concurrentFirst.m_StdOut, "registryId") == firstRegistryId &&
					ExtractJsonField(concurrentSecond.m_StdOut, "registryId") == firstRegistryId &&
					concurrentActive.IsSuccess() &&
					concurrentActive.m_RegistryRef == firstActive.m_RegistryRef,
				"Artifact-root writer lease serializes concurrent complete runtime builds");
		}
	}

	void RunShaderCompilerCliContractSelfTests(SelfTestContext& context) noexcept
	{
		const std::filesystem::path executableDirectory = win32::GetExecutableDirectory();
		const std::filesystem::path cliPath = ShaderCompilerExecutablePath();
		std::error_code errorCode;
		if (!std::filesystem::exists(cliPath, errorCode))
		{
			context.Check(false,
				"Shader compiler TargetName must match the canonical tool identity");
			return;
		}

		const std::filesystem::path sourceRoot = ResolveShaderSourceRoot(executableDirectory);
		const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(errorCode) /
			std::format("GGLabShaderCompilerCli-{}", GetCurrentProcessId());
		context.Check(!errorCode, "CLI contract test resolves a temporary root");
		if (errorCode)
		{
			return;
		}

		ScopedTestDirectory scopedDirectory(tempRoot);
		RunParityTests(context, sourceRoot, tempRoot);
		RunCliBehaviorTests(context, sourceRoot, tempRoot);
		RunJsonProcessContractTests(context, sourceRoot, tempRoot);
		RunCrossProcessHardGateTests(context, sourceRoot, tempRoot);
		RunRuntimeBuildContractTests(context, sourceRoot, tempRoot);
		// Machine describe handshake contract self-tests. The remaining
		// structural no-regression invariants are asserted by the
		// compile/build-runtime matrix and legacy-grammar checks above.
		RunDescribeHandshakeTests(context, sourceRoot, tempRoot);
	}
}
