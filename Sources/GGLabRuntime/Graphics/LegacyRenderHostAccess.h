#pragma once
#include "GGLabRuntime/Graphics/RenderHost.h"

// Transitional legacy access bridge. Ordinary consumers that still require the
// concrete render service must obtain it here instead of widening the Public
// render host contract. The bridge and its call sites are removed in F5/F6.

namespace gglab
{
	class Renderer;

	[[nodiscard]] Renderer* GetLegacyRenderer(RenderHost* host) noexcept;
	[[nodiscard]] const Renderer* GetLegacyRenderer(const RenderHost* host) noexcept;
	[[nodiscard]] Renderer& GetLegacyRenderer(RenderHost& host) noexcept;
}
