#pragma once
#include <Windows.h>

#include <source_location>

namespace gglab
{
	std::string FormatHResult(HRESULT hr);

	bool IsDeviceRemovedHResult(HRESULT hr) noexcept;

	void ReportAndAbort(HRESULT hr, std::string_view context = {},
		std::source_location location = std::source_location::current()) noexcept;

	inline void Ensure(HRESULT hr, std::string_view context = {},
		std::source_location location = std::source_location::current()) noexcept
	{
		if (FAILED(hr))
		{
			ReportAndAbort(hr, context, location);
		}
	}
}

#define GGLAB_HR(expr) (::gglab::Ensure((expr), #expr))
