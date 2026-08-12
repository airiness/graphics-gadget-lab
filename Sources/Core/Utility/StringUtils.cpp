#include "Core/Utility/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>

namespace gglab::utils
{
	std::string StringIdToString(StringID id) noexcept
	{
		if (id.Value() == 0)
		{
			return {};
		}

		const std::string_view name = id.Name();
		if (!name.empty())
		{
			return std::string(name);
		}

		return std::format("0x{:016X}", id.Value());
	}

	bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept
	{
		return lhs.size() == rhs.size() &&
			std::ranges::equal(lhs, rhs,
				[](char left, char right)
				{
					return std::tolower(static_cast<unsigned char>(left)) ==
						std::tolower(static_cast<unsigned char>(right));
				});
	}

	bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix) noexcept
	{
		return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
	}

	bool ContainsIgnoreCase(std::string_view text, std::string_view substring) noexcept
	{
		if (substring.empty())
		{
			return true;
		}

		return std::search(text.begin(), text.end(), substring.begin(), substring.end(),
			[](char left, char right)
			{
				return std::tolower(static_cast<unsigned char>(left)) ==
					std::tolower(static_cast<unsigned char>(right));
			}) != text.end();
	}

	std::string BytesToHexString(std::span<const uint8_t> bytes) noexcept
	{
		constexpr std::string_view HexDigits = "0123456789abcdef";
		std::string text(bytes.size() * 2, '\0');
		for (size_t index = 0; index < bytes.size(); ++index)
		{
			text[index * 2] = HexDigits[bytes[index] >> 4];
			text[index * 2 + 1] = HexDigits[bytes[index] & 0x0f];
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

		const size_t pos = path.find_last_of('/');
		if (pos == std::string_view::npos)
		{
			return path;
		}

		return path.substr(pos + 1);
	}
}
