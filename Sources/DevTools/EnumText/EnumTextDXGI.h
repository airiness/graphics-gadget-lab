#pragma once
#include "DevTools/EnumText/EnumText.h"

#include <dxgiformat.h>

namespace gglab::devtools
{
	template<>
	struct EnumTextTraits<DXGI_FORMAT>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ DXGI_FORMAT_UNKNOWN, "DXGI_FORMAT_UNKNOWN" },
			EnumTextEntry{ DXGI_FORMAT_R16G16_FLOAT, "DXGI_FORMAT_R16G16_FLOAT" },
			EnumTextEntry{ DXGI_FORMAT_R16G16B16A16_FLOAT, "DXGI_FORMAT_R16G16B16A16_FLOAT" },
			EnumTextEntry{ DXGI_FORMAT_R8G8B8A8_UNORM, "DXGI_FORMAT_R8G8B8A8_UNORM" },
			EnumTextEntry{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB" },
		};
		static constexpr std::string_view UnknownText = "Unknown / Other";
	};
}
