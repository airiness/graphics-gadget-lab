#pragma once

namespace gglab::utils
{
	std::wstring ToWideString(std::string_view str) noexcept;
	std::string ToString(std::wstring_view wideStr) noexcept;
}