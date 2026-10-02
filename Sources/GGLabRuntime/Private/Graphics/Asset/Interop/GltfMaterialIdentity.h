#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

struct aiScene;

namespace gglab::asset::interop
{
	inline constexpr size_t NoGltfMaterialSource = std::numeric_limits<size_t>::max();

	// Short names carry source identity through Assimp's public material-name API.
	// Authored names are restored before runtime publication.
	[[nodiscard]] std::string MakeGltfMaterialIdentity(size_t sourceIndex);

	[[nodiscard]] bool ResolveGltfMaterialSources(const aiScene& scene,
		size_t sourceMaterialCount, std::vector<size_t>& sourceIndices,
		std::string& error) noexcept;
}
