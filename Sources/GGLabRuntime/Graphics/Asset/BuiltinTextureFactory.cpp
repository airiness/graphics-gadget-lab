#include "Graphics/Asset/BuiltinTextureFactory.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Asset/Loading/TextureLoader.h"
#include "Graphics/Utility/CubemapUtils.h"
#include "Graphics/Utility/TextureUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		template <typename PixelFunction>
		[[nodiscard]] TextureAssetData MakeTexture2DRgba8(uint32_t width, uint32_t height,
			TextureColorSpace colorSpace, PixelFunction&& pixelFunction) noexcept
		{
			GGLAB_ASSERT(width > 0 && height > 0);
			constexpr size_t formatBytes = 4;
			std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * formatBytes);

			for (uint32_t y = 0; y < height; ++y)
			{
				for (uint32_t x = 0; x < width; ++x)
				{
					const auto color = pixelFunction(x, y);
					uint8_t* pixel =
						pixels.data() + (static_cast<size_t>(y) * width + x) * formatBytes;
					pixel[0] = color[0];
					pixel[1] = color[1];
					pixel[2] = color[2];
					pixel[3] = color[3];
				}
			}

			return TextureLoader::MakeTexture2DRgba8(width, height, pixels, colorSpace);
		}

		[[nodiscard]] Vector3 EvaluateProceduralEnvironment(const Vector3& direction) noexcept
		{
			const float t = std::clamp(direction.m_Y * 0.5f + 0.5f, 0.0f, 1.0f);
			const Vector3 ground(0.04f, 0.035f, 0.03f);
			const Vector3 skyHorizon(0.45f, 0.55f, 0.75f);
			const Vector3 skyZenith(0.08f, 0.18f, 0.45f);
			const Vector3 sky = Lerp(skyHorizon, skyZenith, std::pow(t, 1.5f));
			Vector3 color = Lerp(ground, sky, t);

			const Vector3 sunDirection = Vector3(0.2f, 0.8f, 0.3f).Normalized();
			const float sun = std::pow(std::max(direction.Dot(sunDirection), 0.0f), 512.0f);
			color += sun * Vector3(8.0f, 6.5f, 4.0f);
			return color;
		}

		[[nodiscard]] TextureAssetData MakeProceduralEnvironmentCubemap() noexcept
		{
			constexpr uint32_t faceSize = 16;
			std::vector<float> pixels;
			pixels.reserve(static_cast<size_t>(faceSize) * faceSize * CubemapFaceCount * 4);

			for (uint32_t faceIndex = 0; faceIndex < CubemapFaceCount; ++faceIndex)
			{
				const auto face = static_cast<CubemapFace>(faceIndex);
				for (uint32_t y = 0; y < faceSize; ++y)
				{
					for (uint32_t x = 0; x < faceSize; ++x)
					{
						const float u = (static_cast<float>(x) + 0.5f) / faceSize;
						const float v = (static_cast<float>(y) + 0.5f) / faceSize;
						const Vector3 direction = CubemapFaceUvToDirection(face, Vector2(u, v));
						const Vector3 color = EvaluateProceduralEnvironment(direction);
						pixels.insert(pixels.end(), { color.m_X, color.m_Y, color.m_Z, 1.0f });
					}
				}
			}

			return TextureLoader::MakeTextureCubeRgba16Float(faceSize, pixels);
		}
	}

	std::vector<BuiltinTextureAsset> BuiltinTextureFactory::BuildBootstrapTextures() noexcept
	{
		std::vector<BuiltinTextureAsset> textures;
		textures.reserve(9);

		const auto addGeneratedTexture =
			[&textures](ReservedTextureIDIndex id, std::string_view name, TextureSemantic semantic,
				uint32_t width, uint32_t height, auto&& pixelFunction) noexcept
			{
				textures.push_back({
					.m_Id = id,
					.m_Name = name,
					.m_Semantic = semantic,
					.m_Data =
						MakeTexture2DRgba8(width, height, GetTextureColorSpaceFromSemantic(semantic),
							std::forward<decltype(pixelFunction)>(pixelFunction)),
					});
			};

		addGeneratedTexture(ReservedTextureIDIndex::BaseColorWhite, "BaseColorWhite",
			TextureSemantic::BaseColor, 1, 1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4> { return { 255, 255, 255, 255 }; });

		addGeneratedTexture(ReservedTextureIDIndex::MissingTextureChecker, "MissingTextureChecker",
			TextureSemantic::BaseColor, 64, 64,
			[](uint32_t x, uint32_t y) -> std::array<uint8_t, 4>
			{
				constexpr uint32_t tileSize = 8;
				const bool isPurple = ((x / tileSize) + (y / tileSize)) & 1;
				return isPurple ? std::array<uint8_t, 4>{255, 0, 255, 255}
				: std::array<uint8_t, 4>{ 0, 0, 0, 255 };
			});

		addGeneratedTexture(ReservedTextureIDIndex::NormalFlat, "NormalFlat",
			TextureSemantic::Normal, 1, 1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4> { return { 128, 128, 255, 255 }; });

		addGeneratedTexture(ReservedTextureIDIndex::DefaultMetallicRoughness,
			"DefaultMetallicRoughness", TextureSemantic::MetallicRoughness, 1, 1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
			{
				// The shader multiplies sampled values by material factors, so an
				// absent metallic-roughness texture must use the multiplicative identity.
				return { 0, 255, 255, 255 };
			});

		addGeneratedTexture(ReservedTextureIDIndex::OcclusionWhite, "OcclusionWhite",
			TextureSemantic::Occlusion, 1, 1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4> { return { 255, 255, 255, 255 }; });

		addGeneratedTexture(ReservedTextureIDIndex::EmissiveWhite, "EmissiveWhite",
			TextureSemantic::Emissive, 1, 1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4> { return { 255, 255, 255, 255 }; });

		addGeneratedTexture(ReservedTextureIDIndex::ErrorRed, "ErrorRed",
			TextureSemantic::BaseColor, 1, 1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4> { return { 255, 0, 0, 255 }; });

		addGeneratedTexture(ReservedTextureIDIndex::UVTest, "UVTest", TextureSemantic::UVTest, 256,
			256,
			[](uint32_t x, uint32_t y) -> std::array<uint8_t, 4>
			{
				uint8_t r = static_cast<uint8_t>(x);
				uint8_t g = static_cast<uint8_t>(y);
				uint8_t b = 0;

				constexpr uint32_t gridSize = 32;
				if ((x % gridSize) == 0 || (y % gridSize) == 0)
				{
					r = g = b = 0;
				}

				return { r, g, b, 255 };
			});

		textures.push_back({
			.m_Id = ReservedTextureIDIndex::FallbackEnvironmentCubemap,
			.m_Name = "FallbackEnvironmentCubemap",
			.m_Semantic = TextureSemantic::Environment,
			.m_Data = MakeProceduralEnvironmentCubemap(),
			});

		return textures;
	}
}
