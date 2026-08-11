#include "Core/Utility/StringUtils.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>

namespace gglab::utils
{
	std::wstring ToWideString(std::string_view str) noexcept
	{
		auto need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.data(),
			static_cast<int32_t>(str.size()), nullptr, 0);

		std::wstring wideStr;
		if (need > 0)
		{
			wideStr.resize(need);
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.data(),
				static_cast<int32_t>(str.size()), wideStr.data(), need);
		}
		return wideStr;
	}

	std::string ToString(std::wstring_view wideStr) noexcept
	{
		auto need = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideStr.data(),
			static_cast<int32_t>(wideStr.size()), nullptr, 0, nullptr, nullptr);
		std::string str;
		if (need > 0)
		{
			str.resize(need);
			WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideStr.data(),
				static_cast<int32_t>(wideStr.size()), str.data(), need, nullptr, nullptr);
		}
		return str;
	}

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
