#pragma once

#include "NapaVoxelCore/Meshing/DataOnlyPublication.h"

namespace napa::voxel::testing
{
	class DataOnlyPublicationTestAccess final
	{
	public:
		[[nodiscard]] static ValidationResult PrepareWithAuthoritativeRevision(
			const VoxelWorld& authoritativeWorld, const VoxelMutationResult& mutation,
			const VisibleMeshSet& visible, std::uint64_t authoritativeRevision,
			std::unique_ptr<PendingDataOnlyPublication>& pending);
		[[nodiscard]] static ValidationResult PrepareWithAllocationFailure(
			const VoxelWorld& authoritativeWorld, const VoxelMutationResult& mutation,
			const VisibleMeshSet& visible,
			std::unique_ptr<PendingDataOnlyPublication>& pending);
	};
}
