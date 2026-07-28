#include "Core/Precompiled.h"
#include "Application/ApplicationLaunchOptions.h"
#include "Application/SelfTest/SelfTestRunner.h"
#include "Core/Utility/StringUtils.h"

namespace gglab
{
	namespace
	{
		std::optional<ApplicationStartupDemo> ParseDemo(std::string_view value) noexcept
		{
			if (utils::EqualsIgnoreCase(value, "start") ||
				utils::EqualsIgnoreCase(value, "demo.start"))
			{
				return ApplicationStartupDemo::Start;
			}
			if (utils::EqualsIgnoreCase(value, "playground") ||
				utils::EqualsIgnoreCase(value, "demo.playground"))
			{
				return ApplicationStartupDemo::Playground;
			}
			if (utils::EqualsIgnoreCase(value, "lab") ||
				utils::EqualsIgnoreCase(value, "labhost") ||
				utils::EqualsIgnoreCase(value, "demo.labhost"))
			{
				return ApplicationStartupDemo::LabHost;
			}
			return std::nullopt;
		}
	}

	ApplicationLaunchParseResult ParseApplicationLaunchOptions(
		std::span<const std::string_view> arguments) noexcept
	{
		ApplicationLaunchParseResult result{};
		bool demoSpecified = false;
		for (size_t index = 0; index < arguments.size(); ++index)
		{
			const std::string_view argument = arguments[index];
			if (argument == "--help" || argument == "-h")
			{
				result.m_ShowHelp = true;
				continue;
			}
			if (argument == "--absolute-mouse")
			{
				result.m_Options.m_StartWithAbsoluteMouse = true;
				continue;
			}
			if (argument == "--demo")
			{
				if (demoSpecified)
				{
					result.m_Error = "Option '--demo' may only be specified once.";
					return result;
				}
				if (++index >= arguments.size())
				{
					result.m_Error = "Option '--demo' requires a value.";
					return result;
				}
				const auto demo = ParseDemo(arguments[index]);
				if (!demo)
				{
					result.m_Error = std::format(
						"Unknown demo '{}'. Expected 'start', 'playground', or 'lab'.",
						arguments[index]);
					return result;
				}
				result.m_Options.m_StartupDemo = *demo;
				demoSpecified = true;
				continue;
			}
			if (argument == "--lab")
			{
				if (result.m_Options.m_StartupLabId)
				{
					result.m_Error = "Option '--lab' may only be specified once.";
					return result;
				}
				if (++index >= arguments.size() || arguments[index].empty())
				{
					result.m_Error = "Option '--lab' requires a non-empty Lab ID.";
					return result;
				}
				result.m_Options.m_StartupLabId = std::string(arguments[index]);
				continue;
			}
			if (argument == "--self-test")
			{
				if (result.m_Options.m_SelfTestSuiteId)
				{
					result.m_Error = "Option '--self-test' may only be specified once.";
					return result;
				}
				if (++index >= arguments.size() || arguments[index].empty())
				{
					result.m_Error = "Option '--self-test' requires a non-empty suite ID.";
					return result;
				}
				if (!IsApplicationSelfTestSuiteRegistered(arguments[index]))
				{
					result.m_Error = std::format(
						"Unknown self-test suite '{}'.",
						arguments[index]);
					return result;
				}
				result.m_Options.m_SelfTestSuiteId = std::string(arguments[index]);
				continue;
			}

			result.m_Error = std::format("Unknown option '{}'.", argument);
			return result;
		}

		if (result.m_Options.m_StartupLabId)
		{
			if (demoSpecified &&
				result.m_Options.m_StartupDemo != ApplicationStartupDemo::LabHost)
			{
				result.m_Error =
					"Option '--lab' cannot be combined with a non-LabHost '--demo' value.";
				return result;
			}
			result.m_Options.m_StartupDemo = ApplicationStartupDemo::LabHost;
		}
		if (result.m_Options.m_SelfTestSuiteId &&
			(demoSpecified || result.m_Options.m_StartupLabId ||
				result.m_Options.m_StartWithAbsoluteMouse))
		{
			result.m_Error =
				"Option '--self-test' cannot be combined with interactive startup options.";
		}
		return result;
	}

	std::string_view GetApplicationLaunchUsage() noexcept
	{
		return
			"Usage: GraphicsGadgetLab.exe [options]\n"
			"\n"
			"Options:\n"
			"  --demo <start|playground|lab>   Select the startup demo.\n"
			"  --lab <stable-lab-id>           Start LabHost with the requested Lab.\n"
			"  --absolute-mouse                Start with a visible, uncaptured cursor.\n"
			"  --self-test <suite-id>          Run a headless self-test suite.\n"
			"                                  Available: artifact-cache, asset-data,\n"
			"                                  publication-accounting, rendering-contracts.\n"
			"  --help, -h                      Show this help text.\n";
	}
}
