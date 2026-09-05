#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gglab
{
	struct VulkanQualificationLaunchOptions
	{
		std::optional<std::string> m_AdapterSelector;
		bool m_RunSelfTests = false;
	};

	struct VulkanQualificationLaunchParseResult
	{
		VulkanQualificationLaunchOptions m_Options{};
		std::string m_Error;
		bool m_ShowHelp = false;

		[[nodiscard]] bool IsValid() const noexcept { return m_Error.empty(); }
	};

	[[nodiscard]] VulkanQualificationLaunchParseResult ParseVulkanQualificationLaunchOptions(
		std::span<const std::string_view> arguments) noexcept;
	[[nodiscard]] std::string_view GetVulkanQualificationUsage() noexcept;
}
