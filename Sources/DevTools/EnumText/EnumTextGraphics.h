#pragma once
#include "DevTools/EnumText/EnumText.h"
#include "Graphics/Resource/RenderResourceRegistry.h"

namespace gglab::devtools
{
	template<>
	struct EnumTextTraits<RenderResourceRegistry::IBLPreviewLayout>
	{
		using IBLPreviewLayout = RenderResourceRegistry::IBLPreviewLayout;

		static constexpr std::array Entries = {
			EnumTextEntry{ IBLPreviewLayout::Grid2x3, "2x3 Grid" },
			EnumTextEntry{ IBLPreviewLayout::Cross, "Cross" },
		};
	};
}
