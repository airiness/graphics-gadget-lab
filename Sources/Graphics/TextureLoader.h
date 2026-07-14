#pragma once
#include "Core/Async/ProgressChannel.h"
#include "Graphics/TextureAsset.h"

#include <cstdint>
#include <filesystem>
#include <span>

namespace gglab
{
	enum class TextureMipPolicy : uint8_t
	{
		Preserve,
		GenerateIfMissing,
	};

	struct TextureImportSettings
	{
		TextureSemantic m_Semantic = TextureSemantic::GenericColor;
		TextureMipPolicy m_MipPolicy = TextureMipPolicy::GenerateIfMissing;

		constexpr bool operator==(const TextureImportSettings&) const noexcept = default;
	};

	[[nodiscard]] constexpr TextureImportSettings MakeTextureImportSettings(
		TextureSemantic semantic) noexcept
	{
		return
		{
			.m_Semantic = semantic,
			.m_MipPolicy = semantic == TextureSemantic::Environment ?
				TextureMipPolicy::Preserve :
				TextureMipPolicy::GenerateIfMissing,
		};
	}

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
