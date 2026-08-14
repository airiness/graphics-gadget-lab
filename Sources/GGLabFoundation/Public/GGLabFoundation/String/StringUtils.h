#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace gglab::utils
{
	[[nodiscard]] bool EqualsAsciiIgnoreCase(
		std::string_view lhs, std::string_view rhs) noexcept;
	[[nodiscard]] bool StartsWithAsciiIgnoreCase(
		std::string_view text, std::string_view prefix) noexcept;
	[[nodiscard]] bool ContainsAsciiIgnoreCase(
		std::string_view text, std::string_view substring) noexcept;
	[[nodiscard]] std::string BytesToHexString(std::span<const std::uint8_t> bytes) noexcept;
	[[nodiscard]] std::string_view FindLeaf(std::string_view path) noexcept;

	[[nodiscard]] constexpr const char* BoolToString(bool value) noexcept
	{
		return value ? "Yes" : "No";
	}
}
