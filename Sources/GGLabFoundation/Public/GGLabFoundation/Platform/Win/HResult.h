#pragma once

#include <Windows.h>

#include <source_location>
#include <string>
#include <string_view>

namespace gglab
{
	[[nodiscard]] std::string FormatHResult(HRESULT result);

	[[noreturn]] void ReportAndAbort(HRESULT result, std::string_view context = {},
		std::source_location location = std::source_location::current()) noexcept;

	inline void Ensure(HRESULT result, std::string_view context = {},
		std::source_location location = std::source_location::current()) noexcept
	{
		if (FAILED(result))
		{
			ReportAndAbort(result, context, location);
		}
	}
}

#define GGLAB_HR(expression) (::gglab::Ensure((expression), #expression))
