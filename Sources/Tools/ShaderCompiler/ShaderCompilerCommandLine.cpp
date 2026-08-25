#include "ShaderCompilerCommandLine.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"

#include <string_view>
#include <utility>

namespace gglab
{
	using utils::ToString;

	namespace
	{
		struct ResultFormatScan
		{
			int m_OccurrenceCount = 0;
			bool m_JsonRequested = false;
		};

		[[nodiscard]] ResultFormatScan ScanResultFormat(
			int argumentCount, wchar_t* arguments[]) noexcept
		{
			ResultFormatScan scan{};
			for (int index = 1; index < argumentCount; ++index)
			{
				if (std::wstring_view(arguments[index]) != L"--result-format")
				{
					continue;
				}
				++scan.m_OccurrenceCount;
				if (index + 1 < argumentCount &&
					std::wstring_view(arguments[index + 1]) == L"json")
				{
					scan.m_JsonRequested = true;
				}
			}
			return scan;
		}

		[[nodiscard]] const wchar_t* TakeOptionValue(int argumentCount, wchar_t* arguments[],
			int& index, std::wstring_view option, std::wstring& outValue,
			std::wstring& outError) noexcept
		{
			if (index + 1 >= argumentCount)
			{
				outError = std::wstring(L"Missing value for option: ") + std::wstring(option);
				return nullptr;
			}
			outValue = arguments[++index];
			return outValue.c_str();
		}
	}

	std::wstring ShaderCompilerCommandLineUsage() noexcept
	{
		return L"Usage:\n"
			L"  gglab-shaderc compile --source-root <path> --source <logical-path>\n"
			L"      --stage <vertex|pixel|hull|domain|geometry|mesh|compute>\n"
			L"      --entry <name> --target <gglab-dx12|gglab-vulkan13>\n"
			L"      [--define NAME[=VALUE]]... [--include <path>]...\n"
			L"      [--cache-root <path>] [--artifact-root <path>]\n"
			L"      [--result-format <text|json>]\n"
			L"  gglab-shaderc build-runtime --source-root <path>\n"
			L"      --target <gglab-dx12|gglab-vulkan13> --cache-root <path>\n"
			L"      --artifact-root <path> [--result-format <text|json>]\n"
			L"  gglab-shaderc targets\n"
			L"  gglab-shaderc --version\n"
			L"  gglab-shaderc --help";
	}

	ShaderCompilerCommandLine ParseShaderCompilerCommandLine(
		int argumentCount, wchar_t* arguments[]) noexcept
	{
		ShaderCompilerCommandLine parsed{};
		const ResultFormatScan resultFormat = ScanResultFormat(argumentCount, arguments);
		parsed.m_JsonRequested = resultFormat.m_JsonRequested;
		if (resultFormat.m_OccurrenceCount > 1)
		{
			parsed.m_Command = ShaderCompilerCommand::Compile;
			parsed.m_Error = L"--result-format specified multiple times";
			return parsed;
		}
		if (argumentCount <= 1)
		{
			parsed.m_Command = ShaderCompilerCommand::Help;
			return parsed;
		}

		const std::wstring_view first = arguments[1];
		if (first == L"--version")
		{
			parsed.m_Command = ShaderCompilerCommand::Version;
			return parsed;
		}
		if (first == L"targets")
		{
			parsed.m_Command = ShaderCompilerCommand::Targets;
			return parsed;
		}
		if (first == L"--help" || first == L"help")
		{
			parsed.m_Command = ShaderCompilerCommand::Help;
			return parsed;
		}
		if (first == L"build-runtime")
		{
			parsed.m_Command = ShaderCompilerCommand::BuildRuntime;
			ShaderBuildRuntimeCommandOptions& options = parsed.m_BuildRuntime;
			for (int index = 2; index < argumentCount; ++index)
			{
				const std::wstring_view argument = arguments[index];
				std::wstring value;
				if (argument == L"--source-root")
				{
					if (TakeOptionValue(argumentCount, arguments, index, argument, value,
						parsed.m_Error) == nullptr)
					{
						return parsed;
					}
					options.m_SourceRoot = value;
				}
				else if (argument == L"--target")
				{
					if (TakeOptionValue(argumentCount, arguments, index, argument, value,
						parsed.m_Error) == nullptr)
					{
						return parsed;
					}
					options.m_Target = ToString(value);
				}
				else if (argument == L"--cache-root")
				{
					if (TakeOptionValue(argumentCount, arguments, index, argument, value,
						parsed.m_Error) == nullptr)
					{
						return parsed;
					}
					options.m_CacheRoot = value;
				}
				else if (argument == L"--artifact-root")
				{
					if (TakeOptionValue(argumentCount, arguments, index, argument, value,
						parsed.m_Error) == nullptr)
					{
						return parsed;
					}
					options.m_ArtifactRoot = value;
				}
				else if (argument == L"--result-format")
				{
					if (TakeOptionValue(argumentCount, arguments, index, argument, value,
						parsed.m_Error) == nullptr)
					{
						return parsed;
					}
					options.m_ResultFormat = ToString(value);
				}
				else
				{
					parsed.m_Error = L"Unknown argument: " + std::wstring(argument);
					return parsed;
				}
			}
			if (options.m_SourceRoot.empty() || options.m_Target.empty() ||
				options.m_CacheRoot.empty() || options.m_ArtifactRoot.empty())
			{
				parsed.m_Error = L"build-runtime requires --source-root, --target, "
					L"--cache-root, and --artifact-root";
				return parsed;
			}
			if (options.m_ResultFormat != "text" && options.m_ResultFormat != "json")
			{
				parsed.m_Error = L"Invalid --result-format value: " +
					utils::ToWideString(options.m_ResultFormat);
			}
			return parsed;
		}
		if (first != L"compile")
		{
			parsed.m_Error = L"Unknown command: " + std::wstring(first);
			return parsed;
		}

		parsed.m_Command = ShaderCompilerCommand::Compile;
		ShaderCompileCommandOptions& options = parsed.m_Compile;
		for (int index = 2; index < argumentCount; ++index)
		{
			const std::wstring_view argument = arguments[index];
			std::wstring value;
			if (argument == L"--source-root")
			{
				if (TakeOptionValue(argumentCount, arguments, index, argument, value,
					parsed.m_Error) == nullptr)
				{
					return parsed;
				}
				options.m_SourceRoot = value;
			}
			else if (argument == L"--source")
			{
				if (TakeOptionValue(argumentCount, arguments, index, argument, value,
					parsed.m_Error) == nullptr)
				{
					return parsed;
				}
				options.m_Source = value;
			}
			else if (argument == L"--stage")
			{
				if (TakeOptionValue(argumentCount, arguments, index, argument, value,
					parsed.m_Error) == nullptr)
				{
					return parsed;
				}
				options.m_Stage = ToString(value);
			}
			else if (argument == L"--entry")
			{
				if (TakeOptionValue(argumentCount, arguments, index, argument, value,
					parsed.m_Error) == nullptr)
				{
					return parsed;
				}
				options.m_Entry = ToString(value);
			}
			else if (argument == L"--target")
			{
				if (TakeOptionValue(argumentCount, arguments, index, argument, value,
					parsed.m_Error) == nullptr)
				{
					return parsed;
				}
				options.m_Target = ToString(value);
			}
			else if (argument == L"--define")
			{
				if (TakeOptionValue(argumentCount, arguments, index, argument, value,
					parsed.m_Error) == nullptr)
				{
					return parsed;
				}
				options.m_Defines.push_back(std::move(value));
			}
			else if (argument == L"--include")
			{
				if (TakeOptionValue(argumentCount, arguments, index, argument, value,
					parsed.m_Error) == nullptr)
				{
					return parsed;
				}
				options.m_IncludeDirs.emplace_back(std::move(value));
			}
			else if (argument == L"--cache-root")
			{
				if (TakeOptionValue(argumentCount, arguments, index, argument, value,
					parsed.m_Error) == nullptr)
				{
					return parsed;
				}
				options.m_CacheRoot = value;
			}
			else if (argument == L"--artifact-root")
			{
				if (TakeOptionValue(argumentCount, arguments, index, argument, value,
					parsed.m_Error) == nullptr)
				{
					return parsed;
				}
				options.m_ArtifactRoot = value;
			}
			else if (argument == L"--result-format")
			{
				if (TakeOptionValue(argumentCount, arguments, index, argument, value,
					parsed.m_Error) == nullptr)
				{
					return parsed;
				}
				options.m_ResultFormat = ToString(value);
			}
			else
			{
				parsed.m_Error = L"Unknown argument: " + std::wstring(argument);
				return parsed;
			}
		}

		if (options.m_SourceRoot.empty())
		{
			parsed.m_Error = L"Missing required option: --source-root";
			return parsed;
		}
		if (options.m_Source.empty())
		{
			parsed.m_Error = L"Missing required option: --source";
			return parsed;
		}
		if (options.m_Stage.empty())
		{
			parsed.m_Error = L"Missing required option: --stage";
			return parsed;
		}
		if (options.m_Target.empty())
		{
			parsed.m_Error = L"Missing required option: --target";
			return parsed;
		}
		if (options.m_ResultFormat != "text" && options.m_ResultFormat != "json")
		{
			parsed.m_Error = L"Invalid --result-format value: " +
				utils::ToWideString(options.m_ResultFormat);
			return parsed;
		}
		return parsed;
	}
}
