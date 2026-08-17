#include "ShaderCompilerCommandLine.h"
#include "Compiler/ShaderCompiler.h"
#include "Contracts/ShaderArtifact.h"
#include "Contracts/ShaderCompileTarget.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "Targets/DX12ShaderTarget.h"
#include "Targets/Vulkan13ShaderTarget.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
	constexpr int ExitCodeSuccess = 0;
	constexpr int ExitCodeInvalidCommandLine = 2;
	constexpr int ExitCodeInvalidShaderRequest = 3;
	constexpr int ExitCodeCompileFailed = 4;
	constexpr int ExitCodeArtifactIOFailure = 5;

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
		if (text == "gglab-dx12")
		{
			outProfile = gglab::ShaderTargetProfile::GGLabDX12;
			return true;
		}
		if (text == "gglab-vulkan13")
		{
			outProfile = gglab::ShaderTargetProfile::GGLabVulkan13;
			return true;
		}
		return false;
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

	void AppendJsonEscaped(std::string& out, std::string_view value)
	{
		for (char current : value)
		{
			switch (current)
			{
			case '"':
				out += "\\\"";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				out += current;
				break;
			}
		}
	}

	int PrintTextResult(const gglab::ShaderCompileResult& result,
		const gglab::ShaderResolvedRecipe& recipe, const std::filesystem::path& binaryPath,
		const std::filesystem::path& recordPath, std::wstring_view targetName)
	{
		std::wcout << L"Compiled " << recipe.m_LogicalSourcePath.generic_wstring()
			<< L"::" << recipe.m_Request.m_Entry << L"\n";
		std::wcout << L"Target: " << targetName << L"\n";
		std::wcout << L"Artifact: " << binaryPath.wstring() << L"\n";
		std::wcout << L"Cache: " << (result.m_FromCache ? L"hit" : L"miss") << L"\n";
		std::wcout << L"Cache record: " << recordPath.wstring() << L"\n";
		return ExitCodeSuccess;
	}

	[[nodiscard]] std::string_view CompileStatusText(gglab::ShaderCompileStatus status) noexcept
	{
		switch (status)
		{
		case gglab::ShaderCompileStatus::Success:
			return "success";
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
		}
		return "unknown";
	}

	int PrintJsonFailure(const gglab::ShaderCompilerDiagnostics& diagnostics,
		std::wstring_view targetName)
	{
		std::string json;
		json += "{\"success\":false,";
		json += "\"status\":\"";
		json += CompileStatusText(diagnostics.m_Status);
		json += "\",";
		json += "\"target\":\"";
		AppendJsonEscaped(json, gglab::utils::ToString(targetName));
		json += "\",";
		json += "\"diagnostics\":[{\"message\":\"";
		AppendJsonEscaped(json, gglab::utils::ToString(diagnostics.m_Message));
		json += "\"";
		if (!diagnostics.m_SourceIdentity.empty())
		{
			json += ",\"sourceIdentity\":\"";
			AppendJsonEscaped(json, gglab::utils::ToString(diagnostics.m_SourceIdentity));
			json += "\"";
		}
		json += "}]}";
		std::wcout << gglab::utils::ToWideString(json) << L"\n";
		return ExitCodeForStatus(diagnostics.m_Status);
	}

	int PrintTextFailure(const gglab::ShaderCompilerDiagnostics& diagnostics)
	{
		std::wcerr << diagnostics.m_Message << L"\n";
		return ExitCodeForStatus(diagnostics.m_Status);
	}

	int PrintFailure(const gglab::ShaderCompilerDiagnostics& diagnostics,
		std::wstring_view targetName, bool jsonMode)
	{
		return jsonMode ? PrintJsonFailure(diagnostics, targetName)
			: PrintTextFailure(diagnostics);
	}

	int PrintJsonResult(const gglab::ShaderCompileResult& result,
		const gglab::ShaderResolvedRecipe& recipe, const std::filesystem::path& binaryPath,
		const std::filesystem::path& recordPath, std::wstring_view targetName)
	{
		// binaryHash is the content identity of the committed artifact returned
		// by CompileOrLoad. binaryPath and cacheRecordPath are that artifact's
		// cache-slot locations, so all three fields describe the same committed
		// entry for this completed operation on hit and publication paths.
		std::string json;
		json += "{\"success\":true,";
		json += "\"recipeId\":\"" +
			gglab::Sha256DigestToHex(recipe.m_RecipeId.m_DurableDigest) + "\",";
		json += "\"buildKey\":\"" +
			gglab::Sha256DigestToHex(recipe.m_BuildKey.m_DurableDigest) + "\",";
		json += "\"binaryHash\":\"" +
			gglab::Sha256DigestToHex(result.m_Artifact.m_Manifest.m_BinaryContentDigest.m_Digest) + "\",";
		json += "\"binaryFormat\":\"" +
			std::string(result.m_Artifact.GetBinaryFormat() == gglab::ShaderBinaryFormat::SpirV
				? "spirv" : "dxil") + "\",";
		json += "\"target\":\"";
		AppendJsonEscaped(json, gglab::utils::ToString(targetName));
		json += "\",";
		json += "\"binaryPath\":\"";
		AppendJsonEscaped(json, gglab::utils::ToString(binaryPath.wstring()));
		json += "\",";
		json += "\"cacheRecordPath\":\"";
		AppendJsonEscaped(json, gglab::utils::ToString(recordPath.wstring()));
		json += "\",";
		json += result.m_FromCache ? "\"fromCache\":true," : "\"fromCache\":false,";
		json += "\"diagnostics\":[]}";
		std::wcout << gglab::utils::ToWideString(json) << L"\n";
		return ExitCodeSuccess;
	}

	int RunCompile(const gglab::ShaderCompilerCommandLine& commandLine)
	{
		const gglab::ShaderCompileCommandOptions& options = commandLine.m_Compile;
		gglab::ShaderStage stage{};
		if (!ParseShaderStage(options.m_Stage, stage))
		{
			std::wcerr << L"Unknown stage: " <<
				gglab::utils::ToWideString(options.m_Stage) << L"\n";
			return ExitCodeInvalidCommandLine;
		}
		gglab::ShaderTargetProfile profile{};
		if (!ParseTarget(options.m_Target, profile))
		{
			std::wcerr << L"Unknown target: " <<
				gglab::utils::ToWideString(options.m_Target) << L"\n";
			return ExitCodeInvalidCommandLine;
		}
		const std::wstring targetName = gglab::utils::ToWideString(options.m_Target);

		gglab::ShaderCompiler compiler(options.m_SourceRoot, options.m_CacheRoot);
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

		const gglab::ShaderResolvedRecipe recipe = compiler.Resolve(desc);
		if (!recipe.IsSuccess())
		{
			return PrintFailure(recipe.m_Diagnostics, targetName,
				options.m_ResultFormat == "json");
		}
		const gglab::ShaderCompileResult result = compiler.CompileOrLoad(recipe);
		if (!result.IsSuccess())
		{
			return PrintFailure(result.m_Diagnostics, targetName,
				options.m_ResultFormat == "json");
		}

		const std::filesystem::path binaryPath = compiler.GetCacheBinaryPath(recipe);
		auto recordPath = binaryPath;
		recordPath += L".json";
		if (options.m_ResultFormat == "json")
		{
			return PrintJsonResult(result, recipe, binaryPath, recordPath, targetName);
		}
		return PrintTextResult(result, recipe, binaryPath, recordPath, targetName);
	}

	int RunTargets()
	{
		std::wcout << L"gglab-dx12\n";
		std::wcout << L"gglab-vulkan13\n";
		return ExitCodeSuccess;
	}

	int RunVersion()
	{
		const gglab::ShaderCompilerIdentity identity = gglab::QueryDxcCompilerIdentity();
		std::wcout << L"gglab-shaderc " << gglab::ShaderCompilerToolVersion << L"\n";
		std::wcout << L"Producer: dxc " << identity.m_CanonicalIdentity << L"\n";
		return ExitCodeSuccess;
	}
}

int wmain(int argumentCount, wchar_t* arguments[])
{
	const gglab::ShaderCompilerCommandLine commandLine =
		gglab::ParseShaderCompilerCommandLine(argumentCount, arguments);
	if (!commandLine.IsValid())
	{
		std::wcerr << commandLine.m_Error << L"\n";
		std::wcerr << gglab::ShaderCompilerCommandLineUsage() << L"\n";
		return ExitCodeInvalidCommandLine;
	}

	switch (commandLine.m_Command)
	{
	case gglab::ShaderCompilerCommand::Compile:
		return RunCompile(commandLine);
	case gglab::ShaderCompilerCommand::Targets:
		return RunTargets();
	case gglab::ShaderCompilerCommand::Version:
		return RunVersion();
	case gglab::ShaderCompilerCommand::Help:
	case gglab::ShaderCompilerCommand::None:
		std::wcout << gglab::ShaderCompilerCommandLineUsage() << L"\n";
		return commandLine.m_Command == gglab::ShaderCompilerCommand::Help
			? ExitCodeSuccess : ExitCodeInvalidCommandLine;
	}
	return ExitCodeInvalidCommandLine;
}
