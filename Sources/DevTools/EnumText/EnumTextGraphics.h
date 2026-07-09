#pragma once
#include "DevTools/EnumText/EnumText.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Resource/RenderResourceRegistry.h"

namespace gglab::devtools
{
	template<>
	struct EnumTextTraits<RenderViewID>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RenderViewID::Main, "Main" },
			EnumTextEntry{ RenderViewID::DirectionalShadow, "DirectionalShadow" },
			EnumTextEntry{ RenderViewID::DebugCamera0, "DebugCamera0" },
			EnumTextEntry{ RenderViewID::DebugCamera1, "DebugCamera1" },
			EnumTextEntry{ RenderViewID::DebugCamera2, "DebugCamera2" },
			EnumTextEntry{ RenderViewID::Unknown, "Unknown" },
		};
	};

	template<>
	struct EnumTextTraits<RenderViewVisibilityMode>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RenderViewVisibilityMode::Self, "Self" },
			EnumTextEntry{ RenderViewVisibilityMode::MainCamera, "Main Camera" },
			EnumTextEntry{ RenderViewVisibilityMode::IntersectionWithMainCamera, "Intersection With Main" },
			EnumTextEntry{ RenderViewVisibilityMode::None, "None" },
		};
	};

	template<>
	struct EnumTextTraits<RenderBucket>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RenderBucket::Opaque, "Opaque" },
			EnumTextEntry{ RenderBucket::AlphaTest, "Alpha Test" },
			EnumTextEntry{ RenderBucket::Transparent, "Transparent" },
		};
	};

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
