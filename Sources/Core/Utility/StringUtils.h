#pragma once

namespace gglab::utils
{
	std::wstring ToWideString(std::string_view str) noexcept;
	std::string ToString(std::wstring_view wideStr) noexcept;

	// return the last segment not null
	std::string_view FindLeaf(std::string_view path) noexcept;

	constexpr const char* BoolToString(bool value) noexcept
	{
		return value ? "Yes" : "No";
	}
}
