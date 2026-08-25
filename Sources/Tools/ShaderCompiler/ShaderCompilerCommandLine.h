#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gglab
{
	inline constexpr const wchar_t* ShaderCompilerToolVersion = L"1.0.0";

	// Machine describe handshake wire contract. This is a process-level
	// axis that is deliberately independent of ShaderCompilerToolVersion: it
	// versions the "document format + status vocabulary + exit-code mapping +
	// stdout/stderr channel rules" of the whole machine process contract, and
	// it is the only field (toolVersion, in a different sense) that the
	// machine wire self-describes. Future contract changes bump this from the
	// same constant so the value in the describe document and the consumer
	// support-set gate stay in lockstep.
	inline constexpr int ShaderProcessContractVersion = 1;

	enum class ShaderCompilerCommand : uint8_t
	{
		None,
		Compile,
		BuildRuntime,
		Targets,
		Version,
		Help,
		Describe,
	};

	struct ShaderBuildRuntimeCommandOptions
	{
		std::filesystem::path m_SourceRoot{};
		std::string m_Target{};
		std::filesystem::path m_CacheRoot{};
		std::filesystem::path m_ArtifactRoot{};
		std::string m_ResultFormat{ "text" };
	};

	struct ShaderCompileCommandOptions
	{
		std::filesystem::path m_SourceRoot{};
		std::filesystem::path m_Source{};
		std::string m_Stage{};
		std::string m_Entry{};
		std::string m_Target{};
		std::vector<std::wstring> m_Defines{};
		std::vector<std::filesystem::path> m_IncludeDirs{};
		std::filesystem::path m_CacheRoot{};
		std::filesystem::path m_ArtifactRoot{};
		std::string m_ResultFormat{ "text" };
	};

	struct ShaderCompilerCommandLine
	{
		ShaderCompilerCommand m_Command = ShaderCompilerCommand::None;
		ShaderCompileCommandOptions m_Compile{};
		ShaderBuildRuntimeCommandOptions m_BuildRuntime{};
		// Pre-scanned before normal parsing so every compile usage failure can
		// honor a caller's JSON transport request, including duplicate options.
		bool m_JsonRequested = false;
		std::wstring m_Error{};

		[[nodiscard]] bool IsValid() const noexcept { return m_Error.empty(); }
	};

	// Parses the gglab-shaderc command line. Only high-level shader requests
	// are exposed; compiler-owned flags (-fvk-*, -fspv-*, register shifts) are
	// never accepted.
	[[nodiscard]] ShaderCompilerCommandLine ParseShaderCompilerCommandLine(
		int argumentCount, wchar_t* arguments[]) noexcept;

	[[nodiscard]] std::wstring ShaderCompilerCommandLineUsage() noexcept;
}
