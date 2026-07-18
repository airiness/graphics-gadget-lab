#pragma once
#include "Core/Async/ProgressChannel.h"
#include "Graphics/Asset/TextureAsset.h"

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
			const TextureImportSettings& settings,
			const ProgressReporter& progress = {}) noexcept;

		[[nodiscard]] static TextureAssetData LoadTextureData(
			const std::filesystem::path& texPath,
			TextureColorSpace colorSpace,
			const ProgressReporter& progress = {}) noexcept;

		[[nodiscard]] static TextureAssetData MakeTexture2DRgba8(
			uint32_t width,
			uint32_t height,
			std::span<const uint8_t> pixels,
			TextureColorSpace colorSpace) noexcept;
		[[nodiscard]] static TextureAssetData MakeTextureCubeRgba16Float(
			uint32_t faceSize,
			std::span<const float> rgbaPixels) noexcept;

		[[nodiscard]] static bool SaveTextureDataToDDS(
			const TextureAssetData& textureData,
			const std::filesystem::path& path) noexcept;
	};
}
