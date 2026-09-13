#pragma once
#include "GGLabRuntime/Graphics/Asset/ReservedTexture.h"
#include "GGLabRuntime/Graphics/Asset/TextureAssetViews.h"

#include <cstdint>
#include <optional>

namespace gglab
{
	// Resident texture asset access for passes that bind published textures
	// without reaching the concrete asset manager.
	class RenderTextureAssetAccess
	{
	public:
		virtual ~RenderTextureAssetAccess() = default;

		[[nodiscard]] virtual TextureContentRef GetTextureContentRef(
			TextureID textureId) const noexcept = 0;
		[[nodiscard]] virtual std::optional<ResidentTextureResource>
			GetResidentTextureResource(TextureContentRef content) const noexcept = 0;
		virtual void MarkTextureUsed(TextureID textureId) noexcept = 0;
		[[nodiscard]] virtual uint32_t ResolveSrvIndex(
			TextureID textureId, ReservedTextureIDIndex fallback) const noexcept = 0;
	};
}
