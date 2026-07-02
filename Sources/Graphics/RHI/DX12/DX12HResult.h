#pragma once
#include "Core/HResult.h"

#include <d3d12.h>

namespace gglab
{
	void ReportAndAbortDX12(HRESULT hr, ID3D12Device* device, std::string_view context = {},
		std::source_location location = std::source_location::current()) noexcept;

	inline void EnsureDX12(HRESULT hr, ID3D12Device* device, std::string_view context = {},
		std::source_location location = std::source_location::current()) noexcept
	{
		if (FAILED(hr))
		{
			ReportAndAbortDX12(hr, device, context, location);
		}
	}
}

#define GGLAB_HR_DX(expr, device) (::gglab::EnsureDX12((expr), (device), #expr))
