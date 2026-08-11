#include "Core/Utility/StringUtils.h"

#include <Windows.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace gglab::utils
{
	std::wstring ToWideString(std::string_view str) noexcept
	{
		const int32_t requiredSize = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.data(),
			static_cast<int32_t>(str.size()), nullptr, 0);

		std::wstring wideString;
		if (requiredSize > 0)
		{
			wideString.resize(requiredSize);
			::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.data(),
				static_cast<int32_t>(str.size()), wideString.data(), requiredSize);
		}
		return wideString;
	}

	std::string ToString(std::wstring_view wideString) noexcept
	{
		const int32_t requiredSize = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			wideString.data(), static_cast<int32_t>(wideString.size()), nullptr, 0, nullptr, nullptr);
		std::string string;
		if (requiredSize > 0)
		{
			string.resize(requiredSize);
			::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideString.data(),
				static_cast<int32_t>(wideString.size()), string.data(), requiredSize, nullptr, nullptr);
		}
		return string;
	}
}
