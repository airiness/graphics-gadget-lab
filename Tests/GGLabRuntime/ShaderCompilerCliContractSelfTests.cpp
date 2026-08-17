#include "ShaderCompilerCliContractSelfTests.h"
#include "Compiler/ShaderCompiler.h"
#include "Contracts/ShaderArtifact.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/Shader/ShaderPaths.h"
#include "Targets/DX12ShaderTarget.h"
#include "Targets/Vulkan13ShaderTarget.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
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

		[[nodiscard]] CliRunResult RunCli(const std::vector<std::wstring>& arguments) noexcept
		{
			CliRunResult result{};
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
			commandLine += (win32::GetExecutableDirectory() / L"gglab-shaderc.exe").wstring();
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
			const BOOL created = CreateProcessW(
				(win32::GetExecutableDirectory() / L"gglab-shaderc.exe").c_str(),
				commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
				nullptr, win32::GetExecutableDirectory().c_str(), &startupInfo, &processInfo);
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
			return std::string(json.substr(valueBegin, end - valueBegin));
		}

		[[nodiscard]] bool WriteTextFile(
			const std::filesystem::path& path, std::string_view content) noexcept
		{
			std::filesystem::create_directories(path.parent_path());
			std::ofstream output(path, std::ios::binary);
			output.write(content.data(), static_cast<std::streamsize>(content.size()));
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
					cliBinary.has_value() && cliBinary->SizeInBytes() ==
						runtimeEvidence.m_Binary.SizeInBytes() &&
					(cliBinary->SizeInBytes() == 0 ||
						std::memcmp(cliBinary->Data(), runtimeEvidence.m_Binary.Data(),
							cliBinary->SizeInBytes()) == 0);
			}
			context.Check(allParityCasesMatch,
				"gglab-shaderc produces recipe, build key, digest, and byte-identical artifacts for VS/PS/CS across both targets");
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
				secondRun.m_StdOut.find("Cache: hit") != std::string::npos,
				"CLI text output reports cache miss then hit for the same request");

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

			// JSON mode must stay machine-readable on the failure path: CI and
			// editors consume the same structured diagnostics as the success
			// document instead of scraping stderr.
			const CliRunResult jsonSyntaxError = RunCli({
				L"compile", L"--source-root", badSourceRoot.wstring(), L"--source", L"Bad.hlsl",
				L"--stage", L"compute", L"--entry", L"CSMain", L"--target", L"gglab-dx12",
				L"--cache-root", (tempRoot / L"BadCliCache").wstring(),
				L"--result-format", L"json",
			});
			context.Check(jsonSyntaxError.m_ExitCode == 4 &&
				jsonSyntaxError.m_StdOut.find("\"success\":false") != std::string::npos &&
				jsonSyntaxError.m_StdOut.find("\"status\":\"compile-failed\"") != std::string::npos &&
				jsonSyntaxError.m_StdOut.find("\"diagnostics\":[{\"message\":\"") != std::string::npos,
				"CLI JSON mode emits a structured failure document for compile errors");

			const CliRunResult jsonMissingSource = RunCli({
				L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
				L"Passes/PassDoesNotExist.hlsl", L"--stage", L"vertex", L"--target", L"gglab-dx12",
				L"--result-format", L"json",
			});
			context.Check(jsonMissingSource.m_ExitCode == 3 &&
				jsonMissingSource.m_StdOut.find("\"status\":\"source-not-found\"") != std::string::npos,
				"CLI JSON mode emits a structured failure document for missing sources");

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
			constexpr int GateRoundCount = 3;
			bool allWorkersSucceeded = true;
			bool allThirdRunsHitCache = true;
			bool allHashesConverged = true;
			std::string workerDiagnostics;
			for (int round = 0; round < GateRoundCount; ++round)
			{
				const std::vector<std::wstring> arguments{
					L"compile", L"--source-root", sourceRoot.wstring(), L"--source",
					L"Passes/PassForwardCoverage.hlsl", L"--stage", L"vertex", L"--entry", L"VSMain",
					L"--target", L"gglab-dx12", L"--include", L".", L"--cache-root",
					gateCache.wstring(), L"--result-format", L"json", L"--define",
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
				allWorkersSucceeded &= first.m_ExitCode == 0 && second.m_ExitCode == 0;
				allThirdRunsHitCache &= thirdRun.m_ExitCode == 0 &&
					thirdRun.m_StdOut.find("\"fromCache\":true") != std::string::npos;
				allHashesConverged &= !firstHash.empty() && firstHash == secondHash &&
					secondHash == thirdHash;
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
		}
	}

	void RunShaderCompilerCliContractSelfTests(SelfTestContext& context) noexcept
	{
		const std::filesystem::path executableDirectory = win32::GetExecutableDirectory();
		const std::filesystem::path cliPath = executableDirectory / L"gglab-shaderc.exe";
		std::error_code errorCode;
		if (!std::filesystem::exists(cliPath, errorCode))
		{
			context.Check(false, "gglab-shaderc.exe must be built next to the test executable");
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
		RunCrossProcessHardGateTests(context, sourceRoot, tempRoot);
	}
}
