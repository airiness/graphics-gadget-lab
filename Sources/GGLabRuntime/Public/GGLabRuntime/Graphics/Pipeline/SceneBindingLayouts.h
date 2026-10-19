#pragma once
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"

namespace gglab::scene_bindings
{
	inline constexpr LayoutBindingKey SceneConstants{ 0x100 };
	inline constexpr LayoutBindingKey DrawParameters{ 0x101 };
	inline constexpr LayoutBindingKey PassParameters{ 0x102 };
	inline constexpr LayoutBindingKey InlineParameters{ 0x103 };
	inline constexpr LayoutBindingKey DirectionalShadow{ 0x104 };
	inline constexpr LayoutBindingKey AtmosphereConstants{ 0x105 };
	inline constexpr LayoutBindingKey ObjectTable{ 0x200 };
	inline constexpr LayoutBindingKey MaterialTable{ 0x201 };
	inline constexpr LayoutBindingKey ViewTable{ 0x202 };
	inline constexpr LayoutBindingKey LightTable{ 0x203 };
	inline constexpr LayoutBindingKey ForwardPlusTileHeaders{ 0x204 };
	inline constexpr LayoutBindingKey ForwardPlusTileIndices{ 0x205 };
	inline constexpr LayoutBindingKey GlobalResources{ 0x300 };
	inline constexpr LayoutBindingKey GlobalSamplers{ 0x301 };
	inline constexpr LayoutBindingKey NonFiniteCounters{ 0x400 };

	// The preset has process lifetime; its borrowed RHI debug labels stay valid.
	[[nodiscard]] const ComposedLayout& GetCommonLayoutContract() noexcept;
	[[nodiscard]] LayoutCompositionResult ComposeAtmosphereLayout(const ComposedLayout& common) noexcept;
	[[nodiscard]] LayoutCompositionResult ComposeForwardLayout(
		const ComposedLayout& common, bool forwardPlusLightLists) noexcept;
}
