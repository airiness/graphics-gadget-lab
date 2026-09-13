#pragma once

#include <cstdint>

namespace gglab
{
	enum class AssetResourcePublicationStage : uint8_t
	{
		Unknown,
		Textures,
		Materials,
		Meshes,
		MeshInstances,
		Dependencies,
		Commit,
		ReleaseRetains,
		Count,
	};
}
