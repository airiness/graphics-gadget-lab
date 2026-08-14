#include "GGLabFoundation/String/StringUtils.h"

#include <algorithm>

namespace gglab::utils
{
	namespace
	{
		[[nodiscard]] constexpr char FoldAsciiCase(char value) noexcept
		{
			return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
		}
	}

	bool EqualsAsciiIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept
	{
		return lhs.size() == rhs.size() &&
			std::ranges::equal(lhs, rhs,
				[](char left, char right)
				{
					return FoldAsciiCase(left) == FoldAsciiCase(right);
				});
	}

	bool StartsWithAsciiIgnoreCase(std::string_view text, std::string_view prefix) noexcept
	{
		return text.size() >= prefix.size() &&
			EqualsAsciiIgnoreCase(text.substr(0, prefix.size()), prefix);
	}

	bool ContainsAsciiIgnoreCase(std::string_view text, std::string_view substring) noexcept
	{
		if (substring.empty())
		{
			return true;
		}

		return std::search(text.begin(), text.end(), substring.begin(), substring.end(),
			[](char left, char right)
			{
				return FoldAsciiCase(left) == FoldAsciiCase(right);
			}) != text.end();
	}

	std::string BytesToHexString(std::span<const std::uint8_t> bytes) noexcept
	{
		constexpr std::string_view HexDigits = "0123456789abcdef";
		std::string text(bytes.size() * 2, '\0');
		for (std::size_t index = 0; index < bytes.size(); ++index)
		{
			text[index * 2] = HexDigits[bytes[index] >> 4];
			text[index * 2 + 1] = HexDigits[bytes[index] & 0x0fu];
		}
		return text;
	}

	std::string_view FindLeaf(std::string_view path) noexcept
	{
		while (!path.empty() && path.back() == '/')
		{
			path.remove_suffix(1);
		}

		if (path.empty())
		{
			return {};
		}

		const std::size_t position = path.find_last_of('/');
		return position == std::string_view::npos ? path : path.substr(position + 1);
	}
}
