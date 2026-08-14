#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"

#include <Windows.h>

#include <limits>

namespace gglab::utils
{
	std::wstring ToWideString(std::string_view utf8) noexcept
	{
		if (utf8.empty())
		{
			return {};
		}
		if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		{
			return {};
		}

		const int inputSize = static_cast<int>(utf8.size());
		const int requiredSize =
			::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), inputSize, nullptr, 0);
		if (requiredSize <= 0)
		{
			return {};
		}

		std::wstring result(static_cast<std::size_t>(requiredSize), L'\0');
		return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), inputSize,
			result.data(), requiredSize) == requiredSize
			? result
			: std::wstring{};
	}

	std::string ToString(std::wstring_view utf16) noexcept
	{
		if (utf16.empty())
		{
			return {};
		}
		if (utf16.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		{
			return {};
		}

		const int inputSize = static_cast<int>(utf16.size());
		const int requiredSize = ::WideCharToMultiByte(
			CP_UTF8, WC_ERR_INVALID_CHARS, utf16.data(), inputSize, nullptr, 0, nullptr, nullptr);
		if (requiredSize <= 0)
		{
			return {};
		}

		std::string result(static_cast<std::size_t>(requiredSize), '\0');
		return ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16.data(), inputSize,
			result.data(), requiredSize, nullptr, nullptr) == requiredSize
			? result
			: std::string{};
	}

	std::wstring ToInvariantLowercase(std::wstring_view value) noexcept
	{
		if (value.empty() ||
			value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		{
			return {};
		}

		const int inputSize = static_cast<int>(value.size());
		const int requiredSize = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
			value.data(), inputSize, nullptr, 0, nullptr, nullptr, 0);
		if (requiredSize <= 0)
		{
			return {};
		}

		std::wstring result(static_cast<std::size_t>(requiredSize), L'\0');
		return ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, value.data(), inputSize,
			result.data(), requiredSize, nullptr, nullptr, 0) == requiredSize
			? result
			: std::wstring{};
	}
}
