#pragma once

#include <string>
#include <string_view>

namespace gglab::utils
{
	[[nodiscard]] std::wstring ToWideString(std::string_view utf8) noexcept;
	[[nodiscard]] std::string ToString(std::wstring_view utf16) noexcept;
	[[nodiscard]] std::wstring ToInvariantLowercase(std::wstring_view value) noexcept;
}
