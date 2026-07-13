#pragma once
#include "Graphics/GraphicsTypes.h"

#include <bit>
#include <cstdint>

namespace gglab
{
	[[nodiscard]] constexpr uint32_t CalculateMipLevelCount(uint32_t size) noexcept
	{
		return std::bit_width(size == 0 ? 1u : size);
	}

	[[nodiscard]] constexpr TextureColorSpace GetTextureColorSpaceFromSemantic(TextureSemantic semantic) noexcept
	{
		switch (semantic)
		{
		case TextureSemantic::BaseColor:
		case TextureSemantic::Emissive:
		case TextureSemantic::UVTest:
		case TextureSemantic::GenericColor:
			return TextureColorSpace::SRGB;
		default:
			return TextureColorSpace::Linear;
		}
	}
}
