#pragma once

#include "NapaVoxelCore/Edit/VoxelMutation.h"

#include <cstddef>
#include <cstdint>

namespace napa::voxel::testing
{
	struct VoxelMutationAllocationProbe
	{
		std::size_t m_FailAtPrepareAllocation = 0;
		std::size_t m_PrepareAllocationCount = 0;
		std::size_t m_CommitAllocationCount = 0;
	};

	class VoxelMutationTestAccess final
	{
	public:
		[[nodiscard]] static ValidationResult ApplySphereEditWithAllocationProbe(
			VoxelWorld& world, const SphereEditRequest& request,
			VoxelMutationResult& result, VoxelMutationAllocationProbe& probe);
		[[nodiscard]] static ValidationResult ApplySphereEditWithExhaustedRevision(
			VoxelWorld& world, const SphereEditRequest& request,
			VoxelMutationResult& result);
	};
}
