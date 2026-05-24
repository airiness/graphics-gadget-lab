#pragma once
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
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
