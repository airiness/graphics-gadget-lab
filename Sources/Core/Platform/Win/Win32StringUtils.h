#pragma once

#include <string>
#include <string_view>

namespace gglab::utils
{
	// Windows boundary conversions; UTF-16 only at the Win32 API edge.
	std::wstring ToWideString(std::string_view str) noexcept;
	std::string ToString(std::wstring_view wideStr) noexcept;

	// Invariant-locale lowercase folding for Windows native text (LCMapStringEx).
	std::wstring ToInvariantLowercase(std::wstring_view value) noexcept;
}
