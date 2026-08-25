#include "ShaderCompilerCommandLine.h"
#include "ShaderCompilerProcessFactory.h"
#include "GGLabRuntimeShaderBuild.h"
#include "Artifact/ShaderRuntimeArtifactPublication.h"
#include "Compiler/ShaderCompiler.h"
#include "Contracts/ShaderArtifact.h"
#include "Contracts/ShaderCompileTarget.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/Logging/Log.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "Targets/DX12ShaderTarget.h"
#include "Targets/Vulkan13ShaderTarget.h"
#include "Targets/ShaderTargetWireNames.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace
{
	constexpr int ExitCodeSuccess = 0;
	constexpr int ExitCodeInvalidCommandLine = 2;
	constexpr int ExitCodeInvalidShaderRequest = 3;
	constexpr int ExitCodeCompileFailed = 4;
	constexpr int ExitCodeArtifactIOFailure = 5;
	constexpr int ExitCodeSourceChanged = 6;
	// Handled internal failure floor for the describe document; process
	// crash and CRT abort remain outside its scope. New exit code, not
	// previously allocated for compile / build-runtime.
	constexpr int ExitCodeInternalError = 7;

	// Process-only test seam (no user-facing flag): force the describe
	// handled-internal-failure path so the internal-error / exit 7 wire
	// outcome can be verified deterministically in the black-box matrix.
	// Same confinement style as GGLAB_SHADERC_TEST_FORCE_COMPILER_UNAVAILABLE.
	[[nodiscard]] bool ForceDescribeInternalErrorForTest() noexcept
	{
		wchar_t value[2]{};
		std::size_t length = 0;
		return _wgetenv_s(&length, value, 2, L"GGLAB_SHADERC_TEST_FORCE_DESCRIBE_INTERNAL_ERROR") == 0 &&
			length == 2 && value[0] == L'1';
	}

	// Describe shares the generic "force compiler unavailable" test seam so its
	// producer-unavailable / exit 4 path is deterministic in the black-box
	// matrix. Same env variable name as the compile / build-runtime forced-
	// unavailable seam; no user-facing flag.
	[[nodiscard]] bool ForceDescribeCompilerUnavailableForTest() noexcept
	{
		wchar_t value[2]{};
		std::size_t length = 0;
		return _wgetenv_s(&length, value, 2, L"GGLAB_SHADERC_TEST_FORCE_COMPILER_UNAVAILABLE") == 0 &&
			length == 2 && value[0] == L'1';
	}

	class NullLogSink final : public gglab::LogSink
	{
	public:
		void Write(gglab::LogTag /*tag*/, gglab::LogLevel /*level*/,
			std::string_view /*message*/) noexcept override
		{
		}
	};

	void ConfigureProcessOutput(bool jsonMode)
	{
		if (jsonMode)
		{
			// JSON mode owns stdout exclusively and never lets internal tool logs
			// contaminate the single machine-readable process document.
			gglab::SetLogSink(std::make_shared<NullLogSink>());
		}
	}

	[[nodiscard]] int ExitCodeForStatus(gglab::ShaderCompileStatus status) noexcept
	{
		switch (status)
		{
		case gglab::ShaderCompileStatus::Success:
			return ExitCodeSuccess;
		case gglab::ShaderCompileStatus::InvalidRequest:
		case gglab::ShaderCompileStatus::SourceNotFound:
			return ExitCodeInvalidShaderRequest;
		case gglab::ShaderCompileStatus::CompilerUnavailable:
		case gglab::ShaderCompileStatus::CompileFailed:
			return ExitCodeCompileFailed;
		case gglab::ShaderCompileStatus::ArtifactIOFailure:
			return ExitCodeArtifactIOFailure;
		case gglab::ShaderCompileStatus::SourceChangedDuringCompile:
			return ExitCodeSourceChanged;
		}
		return ExitCodeCompileFailed;
	}

	[[nodiscard]] bool ParseShaderStage(
		std::string_view text, gglab::ShaderStage& outStage) noexcept
	{
		if (text == "vertex")
		{
			outStage = gglab::ShaderStage::Vertex;
			return true;
		}
		if (text == "pixel")
		{
			outStage = gglab::ShaderStage::Pixel;
			return true;
		}
		if (text == "hull")
		{
			outStage = gglab::ShaderStage::Hull;
			return true;
		}
		if (text == "domain")
		{
			outStage = gglab::ShaderStage::Domain;
			return true;
		}
		if (text == "geometry")
		{
			outStage = gglab::ShaderStage::Geometry;
			return true;
		}
		if (text == "mesh")
		{
			outStage = gglab::ShaderStage::Mesh;
			return true;
		}
		if (text == "compute")
		{
			outStage = gglab::ShaderStage::Compute;
			return true;
		}
		return false;
	}

	[[nodiscard]] bool ParseTarget(
		std::string_view text, gglab::ShaderTargetProfile& outProfile) noexcept
	{
		// Accept side of the target wire-name single source of truth; the
		// describe supportedTargets and this accept set are asserted identical.
		return gglab::ShaderTargetWire::Parse(text, outProfile);
	}

	[[nodiscard]] gglab::ShaderCompileTarget MakeCompileTarget(
		gglab::ShaderTargetProfile profile, gglab::ShaderStage stage) noexcept
	{
		switch (profile)
		{
		case gglab::ShaderTargetProfile::GGLabDX12:
			return gglab::MakeDX12CompileTarget(stage);
		case gglab::ShaderTargetProfile::GGLabVulkan13:
			return gglab::MakeVulkan13CompileTarget(stage);
		}
		return {};
	}

	int PrintJsonDocument(const nlohmann::json& document, int exitCode)
	{
		std::cout << document.dump() << '\n';
		return exitCode;
	}

	int PrintJsonUsageFailure(std::wstring_view message)
	{
		return PrintJsonDocument({
			{ "command", "compile" },
			{ "success", false },
			{ "status", "usage-error" },
			{ "exitCode", ExitCodeInvalidCommandLine },
			{ "diagnostics", nlohmann::json::array({ {
				{ "message", gglab::utils::ToString(message) },
			} }) },
		}, ExitCodeInvalidCommandLine);
	}

	int PrintCommandLineFailure(std::wstring_view message, bool jsonMode, bool printUsage)
	{
		if (jsonMode)
		{
			return PrintJsonUsageFailure(message);
		}
		std::wcerr << message << L"\n";
		if (printUsage)
		{
			std::wcerr << gglab::ShaderCompilerCommandLineUsage() << L"\n";
		}
		return ExitCodeInvalidCommandLine;
	}

	int PrintTextResult(const gglab::ShaderCompileResult& result,
		const gglab::ShaderResolvedRecipe& recipe, const std::filesystem::path& binaryPath,
		const std::filesystem::path& recordPath, std::wstring_view targetName,
		const std::optional<gglab::ShaderRuntimeArtifactPublicationResult>& publication)
	{
		std::wcout << L"Compiled " << recipe.m_LogicalSourcePath.generic_wstring()
			<< L"::" << recipe.m_Request.m_Entry << L"\n";
		std::wcout << L"Target: " << targetName << L"\n";
		std::wcout << L"Artifact: " << binaryPath.wstring() << L"\n";
		std::wcout << L"Cache: " << (result.m_FromCache ? L"hit" : L"miss") << L"\n";
		std::wcout << L"Cache record: " << recordPath.wstring() << L"\n";
		if (publication.has_value())
		{
			std::wcout << L"Runtime Artifact ID: " << gglab::utils::ToWideString(
				gglab::Sha256DigestToHex(
					publication->m_ArtifactRef.m_ArtifactId.m_DurableDigest)) << L"\n";
			std::wcout << L"Runtime binary: " <<
				publication->m_Paths.m_BinaryPath.wstring() << L"\n";
			std::wcout << L"Runtime manifest: " <<
				publication->m_Paths.m_ManifestPath.wstring() << L"\n";
		}
		return ExitCodeSuccess;
	}

	[[nodiscard]] std::string_view CompileStatusText(gglab::ShaderCompileStatus status) noexcept
	{
		switch (status)
		{
		case gglab::ShaderCompileStatus::Success:
			return "ok";
		case gglab::ShaderCompileStatus::InvalidRequest:
			return "invalid-request";
		case gglab::ShaderCompileStatus::SourceNotFound:
			return "source-not-found";
		case gglab::ShaderCompileStatus::CompilerUnavailable:
			return "compiler-unavailable";
		case gglab::ShaderCompileStatus::CompileFailed:
			return "compile-failed";
		case gglab::ShaderCompileStatus::ArtifactIOFailure:
			return "artifact-io-failure";
		case gglab::ShaderCompileStatus::SourceChangedDuringCompile:
			return "source-changed";
		}
		return "unknown";
	}

	int PrintJsonFailure(const gglab::ShaderCompilerDiagnostics& diagnostics)
	{
		nlohmann::json diagnostic{
			{ "message", gglab::utils::ToString(diagnostics.m_Message) },
		};
		if (!diagnostics.m_SourceIdentity.empty())
		{
			diagnostic["sourceIdentity"] =
				gglab::utils::ToString(diagnostics.m_SourceIdentity);
		}
		const int exitCode = ExitCodeForStatus(diagnostics.m_Status);
		return PrintJsonDocument({
			{ "command", "compile" },
			{ "success", false },
			{ "status", CompileStatusText(diagnostics.m_Status) },
			{ "exitCode", exitCode },
			{ "diagnostics", nlohmann::json::array({ std::move(diagnostic) }) },
		}, exitCode);
	}

	int PrintTextFailure(const gglab::ShaderCompilerDiagnostics& diagnostics)
	{
		std::wcerr << diagnostics.m_Message << L"\n";
		return ExitCodeForStatus(diagnostics.m_Status);
	}

	int PrintFailure(const gglab::ShaderCompilerDiagnostics& diagnostics, bool jsonMode)
	{
		return jsonMode ? PrintJsonFailure(diagnostics)
			: PrintTextFailure(diagnostics);
	}

	int PrintJsonResult(const gglab::ShaderCompileResult& result,
		const gglab::ShaderResolvedRecipe& recipe, const std::filesystem::path& binaryPath,
		const std::filesystem::path& recordPath, std::wstring_view targetName,
		const std::optional<gglab::ShaderRuntimeArtifactPublicationResult>& publication)
	{
		// binaryHash is the content identity of the committed artifact returned
		// by CompileOrLoad. binaryPath and cacheRecordPath are that artifact's
		// cache-slot locations, so all three fields describe the same committed
		// entry for this completed operation on hit and publication paths.
		nlohmann::json document{
			{ "command", "compile" },
			{ "success", true },
			{ "status", "ok" },
			{ "exitCode", ExitCodeSuccess },
			{ "recipeId", gglab::Sha256DigestToHex(recipe.m_RecipeId.m_DurableDigest) },
			{ "buildKey", gglab::Sha256DigestToHex(recipe.m_BuildKey.m_DurableDigest) },
			{ "binaryHash", gglab::Sha256DigestToHex(
				result.m_Artifact.m_Manifest.m_BinaryContentDigest.m_Digest) },
			{ "binaryFormat", result.m_Artifact.GetBinaryFormat() ==
				gglab::ShaderBinaryFormat::SpirV ? "spirv" : "dxil" },
			{ "target", gglab::utils::ToString(targetName) },
			{ "binaryPath", gglab::utils::ToString(binaryPath.wstring()) },
			{ "cacheRecordPath", gglab::utils::ToString(recordPath.wstring()) },
			{ "fromCache", result.m_FromCache },
			{ "diagnostics", nlohmann::json::array() },
		};
		if (publication.has_value())
		{
			document["artifactId"] = gglab::Sha256DigestToHex(
				publication->m_ArtifactRef.m_ArtifactId.m_DurableDigest);
			document["runtimeArtifactBinaryPath"] = gglab::utils::ToString(
				publication->m_Paths.m_BinaryPath.wstring());
			document["runtimeArtifactManifestPath"] = gglab::utils::ToString(
				publication->m_Paths.m_ManifestPath.wstring());
		}
		return PrintJsonDocument(document, ExitCodeSuccess);
	}

	int RunCompile(const gglab::ShaderCompilerCommandLine& commandLine)
	{
		const gglab::ShaderCompileCommandOptions& options = commandLine.m_Compile;
		gglab::ShaderStage stage{};
		if (!ParseShaderStage(options.m_Stage, stage))
		{
			return PrintCommandLineFailure(L"Unknown stage: " +
				gglab::utils::ToWideString(options.m_Stage),
				commandLine.m_JsonRequested, false);
		}
		gglab::ShaderTargetProfile profile{};
		if (!ParseTarget(options.m_Target, profile))
		{
			return PrintCommandLineFailure(L"Unknown target: " +
				gglab::utils::ToWideString(options.m_Target),
				commandLine.m_JsonRequested, false);
		}
		const std::wstring targetName = gglab::utils::ToWideString(options.m_Target);

		std::unique_ptr<gglab::ShaderCompiler> compiler =
			gglab::CreateShaderCompilerForProcess(options.m_SourceRoot, options.m_CacheRoot);
		gglab::ShaderDesc desc{};
		desc.m_SourcePath = options.m_Source;
		desc.m_Stage = stage;
		desc.m_Target = MakeCompileTarget(profile, stage);
		// Production CLI policy: optimized release compilation.
		desc.m_Target.m_Flags = gglab::ShaderCompileFlags::Optimization;
		desc.m_Entry = gglab::utils::ToWideString(options.m_Entry);
		for (const std::wstring& define : options.m_Defines)
		{
			const std::size_t separator = define.find(L'=');
			desc.m_Defines.push_back({
				.m_Name = separator == std::wstring::npos ? define : define.substr(0, separator),
				.m_Value = separator == std::wstring::npos ? std::wstring{} : define.substr(separator + 1),
				});
		}
		desc.m_IncludeDirs = options.m_IncludeDirs;

		const gglab::ShaderResolvedRecipe recipe = compiler->Resolve(desc);
		if (!recipe.IsSuccess())
		{
			return PrintFailure(recipe.m_Diagnostics, commandLine.m_JsonRequested);
		}
		const gglab::ShaderCompileResult result = compiler->CompileOrLoad(recipe);
		if (!result.IsSuccess())
		{
			return PrintFailure(result.m_Diagnostics, commandLine.m_JsonRequested);
		}
		std::optional<gglab::ShaderRuntimeArtifactPublicationResult> publication;
		if (!options.m_ArtifactRoot.empty())
		{
			publication = gglab::PublishShaderRuntimeArtifact(
				options.m_ArtifactRoot, result.m_Artifact);
			if (!publication->IsSuccess())
			{
				gglab::ShaderCompilerDiagnostics diagnostics{};
				diagnostics.m_Status = gglab::ShaderCompileStatus::ArtifactIOFailure;
				diagnostics.m_Message = L"Failed to publish the Runtime shader artifact";
				diagnostics.m_SourceIdentity = recipe.m_LogicalSourcePath.generic_wstring();
				return PrintFailure(diagnostics, commandLine.m_JsonRequested);
			}
		}

		const std::filesystem::path binaryPath = compiler->GetCacheBinaryPath(recipe);
		auto recordPath = binaryPath;
		recordPath += L".json";
		if (options.m_ResultFormat == "json")
		{
			return PrintJsonResult(
				result, recipe, binaryPath, recordPath, targetName, publication);
		}
		return PrintTextResult(
			result, recipe, binaryPath, recordPath, targetName, publication);
	}

	// Fail closed: only the kinds we explicitly map to a wire name are
	// accepted. An unknown kind resolves to std::nullopt and the caller
	// treats it as a handled internal failure, never a wildcard "dxc".
	[[nodiscard]] std::optional<std::string_view> ProducerKindWireName(
		gglab::ShaderCompilerKind kind) noexcept
	{
		if (kind == gglab::ShaderCompilerKind::Dxc)
		{
			return std::string_view("dxc");
		}
		return std::nullopt;
	}

	int PrintJsonDescribeCompilerUnavailable()
	{
		// Reuses the existing CLI status/exit membership (compiler-unavailable
		// / 4); no business payload, processContractVersion present.
		return PrintJsonDocument({
			{ "command", "describe" },
			{ "success", false },
			{ "status", "compiler-unavailable" },
			{ "exitCode", ExitCodeCompileFailed },
			{ "processContractVersion", gglab::ShaderProcessContractVersion },
			{ "diagnostics", nlohmann::json::array({ {
				{ "message", "DXC producer runtime could not be resolved" },
			} }) },
		}, ExitCodeCompileFailed);
	}

	int PrintJsonDescribeInternalError()
	{
		// Handled internal error floor (exit 7), describe-only.
		return PrintJsonDocument({
			{ "command", "describe" },
			{ "success", false },
			{ "status", "internal-error" },
			{ "exitCode", ExitCodeInternalError },
			{ "processContractVersion", gglab::ShaderProcessContractVersion },
			{ "diagnostics", nlohmann::json::array({ {
				{ "message", "describe internal failure" },
			} }) },
		}, ExitCodeInternalError);
	}

	int PrintJsonDescribeUsageFailure(std::wstring_view message)
	{
		return PrintJsonDocument({
			{ "command", "describe" },
			{ "success", false },
			{ "status", "usage-error" },
			{ "exitCode", ExitCodeInvalidCommandLine },
			{ "processContractVersion", gglab::ShaderProcessContractVersion },
			{ "diagnostics", nlohmann::json::array({ {
				{ "message", gglab::utils::ToString(message) },
			} }) },
		}, ExitCodeInvalidCommandLine);
	}

	int RunDescribe()
	{
		if (ForceDescribeInternalErrorForTest())
		{
			return PrintJsonDescribeInternalError();
		}
		if (ForceDescribeCompilerUnavailableForTest())
		{
			return PrintJsonDescribeCompilerUnavailable();
		}

		try
		{
			const gglab::ShaderCompilerIdentity identity = gglab::QueryDxcCompilerIdentity();
			// An unresolvable producer (empty sentinel or the
			// "unknown" parse-failure sentinel) is a structured failure, never
			// success plus an "unknown" wire value.
			if (identity.m_CanonicalIdentity.empty() ||
				identity.m_CanonicalIdentity == L"unknown")
			{
				return PrintJsonDescribeCompilerUnavailable();
			}
			// Fail closed on a producer kind we do not explicitly map to a
			// wire name; a wildcard "dxc" would silently mislabel an unknown.
			const auto kindWireName = ProducerKindWireName(identity.m_Kind);
			if (!kindWireName.has_value())
			{
				return PrintJsonDescribeInternalError();
			}

			nlohmann::json supportedTargets = nlohmann::json::array();
			for (const std::string& name : gglab::ShaderTargetWire::Names())
			{
				supportedTargets.push_back(name);
			}
			// Field order is contract-stable; every field derives
			// from an existing authority value rather than a CLI-local fact.
			const nlohmann::json document{
				{ "command", "describe" },
				{ "success", true },
				{ "status", "ok" },
				{ "exitCode", ExitCodeSuccess },
				{ "processContractVersion", gglab::ShaderProcessContractVersion },
				{ "toolIdentity", "gglab-shaderc" },
				{ "toolVersion", gglab::utils::ToString(gglab::ShaderCompilerToolVersion) },
				{ "producerKind", std::string(*kindWireName) },
				{ "producerIdentity", gglab::utils::ToString(identity.m_CanonicalIdentity) },
				{ "supportedTargets", std::move(supportedTargets) },
				{ "diagnostics", nlohmann::json::array() },
			};
			return PrintJsonDocument(document, ExitCodeSuccess);
		}
		catch (...)
		{
			// Handled internal failure floor: emit the structured document
			// instead of unwinding an allocation/serialization exception to a
			// process abort. Process crash / CRT abort are outside the contract.
			return PrintJsonDescribeInternalError();
		}
	}

	int RunTargets()
	{
		// Report side of the target wire-name single source of truth; kept
		// human-facing only (consumers must not parse this surface).
		for (const std::string& name : gglab::ShaderTargetWire::Names())
		{
			std::wcout << gglab::utils::ToWideString(name) << L"\n";
		}
		return ExitCodeSuccess;
	}

	[[nodiscard]] int ExitCodeForRuntimeBuildStatus(
		gglab::GGLabRuntimeShaderBuildStatus status) noexcept
	{
		switch (status)
		{
		case gglab::GGLabRuntimeShaderBuildStatus::Succeeded:
			return ExitCodeSuccess;
		case gglab::GGLabRuntimeShaderBuildStatus::InvalidInput:
			return ExitCodeInvalidShaderRequest;
		case gglab::GGLabRuntimeShaderBuildStatus::CompilerUnavailable:
		case gglab::GGLabRuntimeShaderBuildStatus::CompileFailed:
			return ExitCodeCompileFailed;
		case gglab::GGLabRuntimeShaderBuildStatus::WriterUnavailable:
		case gglab::GGLabRuntimeShaderBuildStatus::ArtifactPublicationFailed:
		case gglab::GGLabRuntimeShaderBuildStatus::RegistryBuildFailed:
		case gglab::GGLabRuntimeShaderBuildStatus::RegistryPublicationFailed:
		case gglab::GGLabRuntimeShaderBuildStatus::ActiveRegistryPublicationFailed:
		case gglab::GGLabRuntimeShaderBuildStatus::Failed:
			return ExitCodeArtifactIOFailure;
		}
		return ExitCodeCompileFailed;
	}

	int RunBuildRuntime(const gglab::ShaderCompilerCommandLine& commandLine)
	{
		const gglab::ShaderBuildRuntimeCommandOptions& options = commandLine.m_BuildRuntime;
		gglab::ShaderTargetProfile targetProfile{};
		if (!ParseTarget(options.m_Target, targetProfile))
		{
			return PrintCommandLineFailure(L"Unknown target: " +
				gglab::utils::ToWideString(options.m_Target),
				commandLine.m_JsonRequested, false);
		}
		const gglab::GGLabRuntimeShaderBuildResult result =
			gglab::BuildGGLabRuntimeShaders(targetProfile, options.m_SourceRoot,
				options.m_CacheRoot, options.m_ArtifactRoot);
		const int exitCode = ExitCodeForRuntimeBuildStatus(result.m_Status);
		if (options.m_ResultFormat == "json")
		{
			nlohmann::json document{
				{ "command", "build-runtime" },
				{ "success", result.IsSuccess() },
				{ "status", result.IsSuccess() ? "ok" : "failed" },
				{ "exitCode", exitCode },
				{ "programCount", result.m_ProgramCount },
				{ "diagnostics", result.m_Error.empty()
					? nlohmann::json::array()
					: nlohmann::json::array({ { { "message", result.m_Error } } }) },
			};
			if (result.m_RegistryRef.IsValid())
			{
				document["registryId"] = gglab::Sha256DigestToHex(
					result.m_RegistryRef.m_RegistryId.m_DurableDigest);
			}
			return PrintJsonDocument(document, exitCode);
		}
		if (!result.IsSuccess())
		{
			std::cerr << result.m_Error << '\n';
			return exitCode;
		}
		std::cout << "Built and activated " << result.m_ProgramCount
			<< " GGLab shader programs.\nRegistry: "
			<< gglab::Sha256DigestToHex(
				result.m_RegistryRef.m_RegistryId.m_DurableDigest) << '\n';
		return ExitCodeSuccess;
	}

	int RunVersion()
	{
		const gglab::ShaderCompilerIdentity identity = gglab::QueryDxcCompilerIdentity();
		std::wcout << L"gglab-shaderc " << gglab::ShaderCompilerToolVersion << L"\n";
		if (identity.m_CanonicalIdentity.empty() ||
			identity.m_CanonicalIdentity == L"unknown")
		{
			// Human-facing distinction for an unresolvable producer; the wire
			// (machine) contract is unchanged by this.
			std::wcout << L"Producer: unavailable\n";
		}
		else
		{
			std::wcout << L"Producer: dxc " << identity.m_CanonicalIdentity << L"\n";
		}
		return ExitCodeSuccess;
	}
}

int wmain(int argumentCount, wchar_t* arguments[])
{
	const gglab::ShaderCompilerCommandLine commandLine =
		gglab::ParseShaderCompilerCommandLine(argumentCount, arguments);
	// describe is JSON-implicit: its machine channel (exactly one stdout
	// document, empty stderr) is owned by the command itself, not by
	// --result-format (which it does not accept).
	const bool jsonRequested = commandLine.m_JsonRequested ||
		commandLine.m_Command == gglab::ShaderCompilerCommand::Describe;
	ConfigureProcessOutput(jsonRequested);
	if (!commandLine.IsValid())
	{
		if (commandLine.m_Command == gglab::ShaderCompilerCommand::Describe)
		{
			return PrintJsonDescribeUsageFailure(commandLine.m_Error);
		}
		return PrintCommandLineFailure(commandLine.m_Error, jsonRequested, true);
	}

	switch (commandLine.m_Command)
	{
	case gglab::ShaderCompilerCommand::Compile:
		return RunCompile(commandLine);
	case gglab::ShaderCompilerCommand::BuildRuntime:
		return RunBuildRuntime(commandLine);
	case gglab::ShaderCompilerCommand::Targets:
		return RunTargets();
	case gglab::ShaderCompilerCommand::Version:
		return RunVersion();
	case gglab::ShaderCompilerCommand::Describe:
		return RunDescribe();
	case gglab::ShaderCompilerCommand::Help:
	case gglab::ShaderCompilerCommand::None:
		std::wcout << gglab::ShaderCompilerCommandLineUsage() << L"\n";
		return commandLine.m_Command == gglab::ShaderCompilerCommand::Help
			? ExitCodeSuccess : ExitCodeInvalidCommandLine;
	}
	return ExitCodeInvalidCommandLine;
}
