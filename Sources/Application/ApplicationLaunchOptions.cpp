#include "Core/Precompiled.h"
#include "Application/ApplicationLaunchOptions.h"

#include <cctype>

namespace gglab
{
	namespace
	{
		bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept
		{
			return lhs.size() == rhs.size() &&
				std::ranges::equal(lhs, rhs, [](char left, char right)
					{
						return std::tolower(static_cast<unsigned char>(left)) ==
							std::tolower(static_cast<unsigned char>(right));
					});
		}

		std::optional<ApplicationStartupDemo> ParseDemo(std::string_view value) noexcept
		{
			if (EqualsIgnoreCase(value, "playground") ||
				EqualsIgnoreCase(value, "demo.playground"))
			{
				return ApplicationStartupDemo::Playground;
			}
			if (EqualsIgnoreCase(value, "lab") ||
				EqualsIgnoreCase(value, "labhost") ||
				EqualsIgnoreCase(value, "demo.labhost"))
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
						"Unknown demo '{}'. Expected 'playground' or 'lab'.",
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

			result.m_Error = std::format("Unknown option '{}'.", argument);
			return result;
		}

		if (result.m_Options.m_StartupLabId)
		{
			if (demoSpecified &&
				result.m_Options.m_StartupDemo == ApplicationStartupDemo::Playground)
			{
				result.m_Error = "Option '--lab' cannot be combined with '--demo playground'.";
				return result;
			}
			result.m_Options.m_StartupDemo = ApplicationStartupDemo::LabHost;
		}
		return result;
	}

	std::string_view GetApplicationLaunchUsage() noexcept
	{
		return
			"Usage: GraphicsGadgetLab.exe [options]\n"
			"\n"
			"Options:\n"
			"  --demo <playground|lab>  Select the startup demo.\n"
			"  --lab <stable-lab-id>    Start LabHost with the requested Lab.\n"
			"  --absolute-mouse         Start with a visible, uncaptured cursor.\n"
			"  --help, -h               Show this help text.\n";
	}
}
