#include "QualificationLaunchOptions.h"

#include <cstddef>
#include <format>

namespace gglab
{
	VulkanQualificationLaunchParseResult ParseVulkanQualificationLaunchOptions(
		std::span<const std::string_view> arguments) noexcept
	{
		VulkanQualificationLaunchParseResult result{};
		for (size_t index = 0; index < arguments.size(); ++index)
		{
			const std::string_view argument = arguments[index];
			if (argument == "--help" || argument == "-h")
			{
				result.m_ShowHelp = true;
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
			if (argument == "--self-test")
			{
				if (result.m_Options.m_RunSelfTests)
				{
					result.m_Error = "Option '--self-test' may only be specified once.";
					return result;
				}
				result.m_Options.m_RunSelfTests = true;
				continue;
			}

			result.m_Error = std::format("Unknown option '{}'.", argument);
			return result;
		}

		if (result.m_Options.m_RunSelfTests && result.m_Options.m_AdapterSelector)
		{
			result.m_Error = "Option '--self-test' cannot be combined with '--adapter'.";
		}
		return result;
	}

	std::string_view GetVulkanQualificationUsage() noexcept
	{
		return "Usage: GGLabVulkanQualification.exe [options]\n"
			"\n"
			"Options:\n"
			"  --adapter <index|prefix>  Select a Vulkan adapter by enumeration index\n"
			"                            or device UUID/name prefix.\n"
			"  --self-test               Run headless qualification contract tests.\n"
			"  --help, -h                Show this help text.\n"
			"Requires the Vulkan SDK 1.3.296 for Vulkan qualification builds\n"
			"(GGLAB_ENABLE_VULKAN=1, the default).\n";
	}
}
