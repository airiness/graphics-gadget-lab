#pragma once

#include <string_view>

namespace gglab
{
	[[nodiscard]] bool IsApplicationSelfTestSuiteRegistered(std::string_view suiteId) noexcept;
	[[nodiscard]] bool RunApplicationSelfTestSuite(std::string_view suiteId) noexcept;
}
