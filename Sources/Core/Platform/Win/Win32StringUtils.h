#pragma once

#include <string>
#include <string_view>

namespace gglab::utils
{
	// Invariant-locale lowercase folding for Windows native text (LCMapStringEx).
	std::wstring ToInvariantLowercase(std::wstring_view value) noexcept;
}
