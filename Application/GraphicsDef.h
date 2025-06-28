#pragma once
#include <cstdint>

namespace graphicsGadgetLab
{
	using TextureID = uint32_t;
	inline constexpr TextureID InvalidTextureID = (TextureID)(-1);
	inline constexpr TextureID ReservedTextureID = (TextureID)(5);

	using MeshID = uint32_t;
	inline constexpr MeshID IndividualMeshID = (MeshID)(1);
	inline constexpr MeshID InvalidMeshID = IndividualMeshID + 1;

	using MaterialID = uint32_t;
	inline constexpr MaterialID InvalidMaterialID = (MaterialID)(-1);
}