#pragma once

#include <string_view>

namespace gglab
{
	inline constexpr std::string_view AllApplicationSelfTestsSelection = "all";
	inline constexpr std::string_view ApplicationPathCompositionSelfTestSelection =
		"app-path-composition";

	struct RuntimePaths;

	[[nodiscard]] bool IsApplicationSelfTestSelectionValid(std::string_view selection) noexcept;
	[[nodiscard]] bool RunApplicationSelfTests(std::string_view selection) noexcept;
	[[nodiscard]] bool RunApplicationPathCompositionSelfTest(
		const RuntimePaths& runtimePaths) noexcept;
}
