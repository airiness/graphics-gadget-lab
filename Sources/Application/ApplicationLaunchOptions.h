#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gglab
{
	enum class ApplicationStartupDemo : uint8_t
	{
		Playground,
		LabHost,
	};

	struct ApplicationLaunchOptions
	{
		ApplicationStartupDemo m_StartupDemo = ApplicationStartupDemo::Playground;
		std::optional<std::string> m_StartupLabId;
		bool m_StartWithAbsoluteMouse = false;
	};

	struct ApplicationLaunchParseResult
	{
		ApplicationLaunchOptions m_Options{};
		std::string m_Error;
		bool m_ShowHelp = false;

		[[nodiscard]] bool IsValid() const noexcept { return m_Error.empty(); }
	};

	ApplicationLaunchParseResult ParseApplicationLaunchOptions(
		std::span<const std::string_view> arguments) noexcept;
	std::string_view GetApplicationLaunchUsage() noexcept;
}
