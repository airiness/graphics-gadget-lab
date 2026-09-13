#pragma once
#include <cstdint>

namespace gglab
{
	enum class TextureColorSpace : uint8_t
	{
		Linear,
		SRGB
	};

	enum class TextureSemantic : uint32_t
	{
		BaseColor,
		Emissive,
		Normal,
		MetallicRoughness,
		Occlusion,
		UVTest,
		Environment,
		GenericColor,
		GenericData,
		Unknown
	};

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
		return {
			.m_Semantic = semantic,
			.m_MipPolicy = semantic == TextureSemantic::Environment
							   ? TextureMipPolicy::Preserve
							   : TextureMipPolicy::GenerateIfMissing,
		};
	}

	inline constexpr uint32_t CubemapFaceCount = 6u;
	enum class CubemapFace : uint8_t
	{
		// Matches the D3D TextureCube array-slice order.
		PositiveX,
		NegativeX,
		PositiveY,
		NegativeY,
		PositiveZ,
		NegativeZ,

		Count
	};
	static_assert(static_cast<uint32_t>(CubemapFace::Count) == CubemapFaceCount);
}
