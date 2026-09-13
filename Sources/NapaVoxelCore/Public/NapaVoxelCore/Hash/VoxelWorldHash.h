#pragma once

#include "NapaVoxelCore/Validation/ValidationResult.h"

#include <cstdint>

namespace napa::voxel
{
	class VoxelWorld;

	[[nodiscard]] ValidationResult ComputeLogicalVoxelWorldHash(
		const VoxelWorld& world, std::uint64_t& hash) noexcept;
}
