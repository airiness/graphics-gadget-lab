#include "Core/Precompiled.h"
#include "Core/Utility/StringUtils.h"

namespace gglab::utils
{
	std::wstring ToWideString(std::string_view str) noexcept
	{
		auto need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			str.data(), static_cast<int32_t>(str.size()), nullptr, 0);

		std::wstring wideStr;
		if (need > 0)
		{
			wideStr.resize(need);
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
				str.data(), static_cast<int32_t>(str.size()), wideStr.data(), need);
		}
		return wideStr;
	}

	std::string ToString(std::wstring_view wideStr) noexcept
	{
		auto need = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			wideStr.data(), static_cast<int32_t>(wideStr.size()), nullptr, 0, nullptr, nullptr);
		std::string str;
		if (need > 0)
		{
			str.resize(need);
			WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
				wideStr.data(), static_cast<int32_t>(wideStr.size()), str.data(), need, nullptr, nullptr);
		}
		return str;
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

