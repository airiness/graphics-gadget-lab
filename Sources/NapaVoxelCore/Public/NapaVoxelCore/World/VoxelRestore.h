#pragma once

#include "NapaVoxelCore/Edit/VoxelMutation.h"

namespace napa::voxel
{
	class VoxelWorld;

	[[nodiscard]] ValidationResult RestoreAll(
		VoxelWorld& world, VoxelMutationResult& result);
	[[nodiscard]] ValidationResult RestoreSampleOwnerChunk(
		VoxelWorld& world, ChunkCoord chunk, VoxelMutationResult& result);
	[[nodiscard]] ValidationResult RestoreRegion(
		VoxelWorld& world, const SampleAabb& region, VoxelMutationResult& result);
}
