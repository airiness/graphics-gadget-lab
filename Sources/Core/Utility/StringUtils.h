#pragma once
#include "Core/StringId.h"

namespace gglab::utils
{
	std::wstring ToWideString(std::string_view str) noexcept;
	std::string ToString(std::wstring_view wideStr) noexcept;
	std::string StringIdToString(StringID id) noexcept;
	bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept;
	bool ContainsIgnoreCase(std::string_view text, std::string_view substring) noexcept;

	// return the last segment not null
	std::string_view FindLeaf(std::string_view path) noexcept;

	constexpr const char* BoolToString(bool value) noexcept
	{
		return value ? "Yes" : "No";
	}
}
