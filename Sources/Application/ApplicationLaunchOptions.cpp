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
			if (argument == "--rhi")
			{
				if (result.m_Options.m_RhiBackendSpecified)
				{
					result.m_Error = "Option '--rhi' may only be specified once.";
					return result;
				}
				if (++index >= arguments.size() || arguments[index].empty())
				{
					result.m_Error = "Option '--rhi' requires a backend name ('dx12' or 'vulkan').";
					return result;
				}
				if (utils::EqualsIgnoreCase(arguments[index], "dx12"))
				{
					result.m_Options.m_RhiBackend = RHIBackendType::DX12;
				}
				else if (utils::EqualsIgnoreCase(arguments[index], "vulkan"))
				{
					result.m_Options.m_RhiBackend = RHIBackendType::Vulkan;
				}
				else
				{
					result.m_Error = std::format(
						"Unknown RHI backend '{}'. Expected 'dx12' or 'vulkan'.",
						arguments[index]);
					return result;
				}
				result.m_Options.m_RhiBackendSpecified = true;
				continue;
			}
			if (argument == "--list-adapters")
			{
				if (result.m_Options.m_ListAdapters)
				{
					result.m_Error = "Option '--list-adapters' may only be specified once.";
					return result;
				}
				result.m_Options.m_ListAdapters = true;
				continue;
			}
			if (argument == "--adapter")
			{
				if (result.m_Options.m_AdapterSelector)
				{
					result.m_Error = "Option '--adapter' may only be specified once.";
					return result;
				}
				if (++index >= arguments.size() || arguments[index].empty())
				{
					result.m_Error =
						"Option '--adapter' requires an enumeration index or identity prefix.";
					return result;
				}
				result.m_Options.m_AdapterSelector = std::string(arguments[index]);
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
					result.m_Error =
						std::format("Unknown demo '{}'. Expected 'start', 'playground', or 'lab'.",
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
				if (result.m_Options.m_SelfTestSelection)
				{
					result.m_Error = "Option '--self-test' may only be specified once.";
					return result;
				}
				if (++index >= arguments.size() || arguments[index].empty())
				{
					result.m_Error = "Option '--self-test' requires a non-empty suite ID.";
					return result;
				}
				if (!IsApplicationSelfTestSelectionValid(arguments[index]))
				{
					result.m_Error = std::format("Unknown self-test selection '{}'.", arguments[index]);
					return result;
				}
				result.m_Options.m_SelfTestSelection = std::string(arguments[index]);
				continue;
			}

			result.m_Error = std::format("Unknown option '{}'.", argument);
			return result;
		}

		if (result.m_Options.m_StartupLabId)
		{
			if (demoSpecified && result.m_Options.m_StartupDemo != ApplicationStartupDemo::LabHost)
			{
				result.m_Error =
					"Option '--lab' cannot be combined with a non-LabHost '--demo' value.";
				return result;
			}
			result.m_Options.m_StartupDemo = ApplicationStartupDemo::LabHost;
		}
		if (result.m_Options.m_SelfTestSelection &&
			(demoSpecified || result.m_Options.m_StartupLabId ||
				result.m_Options.m_StartWithAbsoluteMouse ||
				result.m_Options.m_RhiBackendSpecified || result.m_Options.m_ListAdapters ||
				result.m_Options.m_AdapterSelector))
		{
			result.m_Error =
				"Option '--self-test' cannot be combined with interactive startup options.";
			return result;
		}
		if (result.m_Options.m_AdapterSelector && result.m_Options.m_ListAdapters)
		{
			result.m_Error = "Option '--adapter' cannot be combined with '--list-adapters'.";
			return result;
		}
		if (result.m_Options.m_ListAdapters && result.m_Options.m_RhiBackendSpecified &&
			result.m_Options.m_RhiBackend != RHIBackendType::Vulkan)
		{
			result.m_Error =
				"Option '--list-adapters' requires the Vulkan backend and cannot be combined with '--rhi dx12'.";
			return result;
		}
		if (result.m_Options.m_AdapterSelector &&
			(!result.m_Options.m_RhiBackendSpecified ||
				result.m_Options.m_RhiBackend != RHIBackendType::Vulkan))
		{
			result.m_Error =
				"Option '--adapter' requires an explicit '--rhi vulkan'.";
			return result;
		}
		return result;
	}

	std::string_view GetApplicationLaunchUsage() noexcept
	{
		return "Usage: GraphicsGadgetLab.exe [options]\n"
			"\n"
			"Options:\n"
			"  --demo <start|playground|lab>   Select the startup demo.\n"
			"  --lab <stable-lab-id>           Start LabHost with the requested Lab.\n"
			"  --absolute-mouse                Start with a visible, uncaptured cursor.\n"
			"  --rhi <dx12|vulkan>             Select the RHI backend (default: dx12).\n"
			"                                  Explicit 'vulkan' never falls back to DX12.\n"
			"  --list-adapters                 Enumerate Vulkan adapters with profile\n"
			"                                  evaluation and exit.\n"
			"  --adapter <index|prefix>        Select a Vulkan adapter by enumeration\n"
			"                                  index or device UUID/name prefix.\n"
			"                                  Requires an explicit --rhi vulkan.\n"
			"  --self-test <suite-id|all>      Run one or all headless self-test suites.\n"
			"                                  Available: artifact-cache, asset-data,\n"
			"                                  publication-accounting, rendering-contracts,\n"
			"                                  napa-voxel, vulkan-contracts, all.\n"
			"  --help, -h                      Show this help text.\n"
			"Requires the Vulkan SDK 1.3.296 for --rhi vulkan / --list-adapters builds\n"
			"(GGLAB_ENABLE_VULKAN=1, the default).\n";
	}
}
