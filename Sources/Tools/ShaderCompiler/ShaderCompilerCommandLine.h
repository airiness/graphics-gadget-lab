#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
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

	// Canonical machine vocabulary for commands that own JSON envelopes.
	// Human-only commands deliberately have no wire name.
	[[nodiscard]] std::string_view ShaderCompilerCommandWireName(
		ShaderCompilerCommand command) noexcept;

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
