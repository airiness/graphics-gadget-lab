#pragma once
#include "Graphics/TextureAsset.h"

#include <cstdint>
#include <filesystem>
#include <span>

namespace gglab
{
	class TextureLoader
	{
	public:
		[[nodiscard]] static TextureAssetData LoadTextureData(
			const std::filesystem::path& texPath,
			TextureColorSpace colorSpace) noexcept;

		[[nodiscard]] static TextureAssetData MakeTexture2DRgba8(
			uint32_t width,
			uint32_t height,
			std::span<const uint8_t> pixels,
			TextureColorSpace colorSpace) noexcept;
	};
}
