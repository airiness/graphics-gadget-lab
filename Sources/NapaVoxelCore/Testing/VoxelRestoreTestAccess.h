#pragma once

#include "NapaVoxelCore/Testing/VoxelMutationTestAccess.h"
#include "NapaVoxelCore/World/VoxelRestore.h"

namespace napa::voxel::testing
{
	using VoxelRestoreAllocationProbe = VoxelOperationAllocationProbe;

	class VoxelRestoreTestAccess final
	{
	public:
		[[nodiscard]] static ValidationResult RestoreAllWithAllocationProbe(
			VoxelWorld& world, VoxelMutationResult& result,
			VoxelRestoreAllocationProbe& probe);
		[[nodiscard]] static ValidationResult RestoreAllWithExhaustedRevision(
			VoxelWorld& world, VoxelMutationResult& result);
	};
}
