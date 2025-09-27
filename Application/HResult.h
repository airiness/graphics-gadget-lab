#pragma once
#include <source_location>

namespace gglab
{
	std::string FormatHResult(HRESULT hr);

	bool IsDeviceRemovedHResult(HRESULT hr) noexcept;

	void ReportAndAbort(HRESULT hr, ID3D12Device* device = nullptr, std::string_view context = {},
		std::source_location location = std::source_location::current()) noexcept;

	inline void Ensure(HRESULT hr, std::string_view context = {},
		std::source_location location = std::source_location::current()) noexcept
	{
		if (FAILED(hr))
		{
			ReportAndAbort(hr, nullptr, context, location);
		}
	}
	
	inline void Ensure(HRESULT hr, ID3D12Device* device, std::string_view context = {},
		std::source_location location = std::source_location::current()) noexcept
	{
		if (FAILED(hr))
		{
			ReportAndAbort(hr, device, context, location);
		}
	}
}

#define GGLAB_HR(expr) (::gglab::Ensure((expr), #expr))
#define GGLAB_HR_DX(expr, device) (::gglab::Ensure((expr), (device), #expr))