#pragma once

#include <string_view>

namespace gglab
{
	inline constexpr std::string_view AllApplicationSelfTestsSelection = "all";

	[[nodiscard]] bool IsApplicationSelfTestSelectionValid(std::string_view selection) noexcept;
	[[nodiscard]] bool RunApplicationSelfTests(std::string_view selection) noexcept;
}
