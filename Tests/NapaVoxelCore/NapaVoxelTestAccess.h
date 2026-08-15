#pragma once

#include "NapaVoxelCore/Edit/VoxelMutation.h"
#include "NapaVoxelCore/Edit/VoxelOperationDetail.h"
#include "NapaVoxelCore/Meshing/DataOnlyPublication.h"
#include "NapaVoxelCore/Meshing/DataOnlyPublicationDetail.h"
#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/VoxelRestore.h"

#include <cstdint>
#include <memory>

namespace napa::voxel::testing
{
	// Fault-injection seam types. The canonical probe lives in
	// napa::voxel::detail and is consulted by production allocation paths.
	using VoxelOperationAllocationProbe = detail::VoxelOperationAllocationProbe;
	using VoxelMutationAllocationProbe = VoxelOperationAllocationProbe;
	using VoxelRestoreAllocationProbe = VoxelOperationAllocationProbe;

	// Fault-injection drivers. Owned by the test target; they configure the
	// published detail seams and call the public API.
	[[nodiscard]] ValidationResult ApplySphereEditWithAllocationProbe(
		VoxelWorld& world, const SphereEditRequest& request,
		VoxelMutationResult& result, VoxelMutationAllocationProbe& probe);
	[[nodiscard]] ValidationResult ApplySphereEditWithExhaustedRevision(
		VoxelWorld& world, const SphereEditRequest& request, VoxelMutationResult& result);
	[[nodiscard]] ValidationResult RestoreAllWithAllocationProbe(
		VoxelWorld& world, VoxelMutationResult& result, VoxelRestoreAllocationProbe& probe);
	[[nodiscard]] ValidationResult RestoreAllWithExhaustedRevision(
		VoxelWorld& world, VoxelMutationResult& result);
	[[nodiscard]] ValidationResult PrepareWithAuthoritativeRevision(
		const VoxelWorld& authoritativeWorld, const VoxelMutationResult& mutation,
		const VisibleMeshSet& visible, std::uint64_t authoritativeRevision,
		std::unique_ptr<PendingDataOnlyPublication>& pending);
	[[nodiscard]] ValidationResult PrepareWithAllocationFailure(
		const VoxelWorld& authoritativeWorld, const VoxelMutationResult& mutation,
		const VisibleMeshSet& visible,
		std::unique_ptr<PendingDataOnlyPublication>& pending);
}
