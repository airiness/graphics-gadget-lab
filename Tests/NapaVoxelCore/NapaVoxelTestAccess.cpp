#include "NapaVoxelTestAccess.h"

namespace napa::voxel::testing
{
	ValidationResult ApplySphereEditWithAllocationProbe(
		VoxelWorld& world, const SphereEditRequest& request,
		VoxelMutationResult& result, VoxelMutationAllocationProbe& probe)
	{
		if (detail::ActiveOperationAllocationProbe != nullptr)
		{
			return { ValidationError::InvalidVoxelMutation };
		}

		probe.m_PrepareAllocationCount = 0;
		probe.m_CommitAllocationCount = 0;
		detail::IsVoxelOperationCommitPhase = false;
		detail::ActiveOperationAllocationProbe = &probe;
		const ValidationResult mutationResult =
			napa::voxel::ApplySphereEdit(world, request, result);
		detail::ActiveOperationAllocationProbe = nullptr;
		detail::IsVoxelOperationCommitPhase = false;
		return mutationResult;
	}

	ValidationResult ApplySphereEditWithExhaustedRevision(
		VoxelWorld& world, const SphereEditRequest& request, VoxelMutationResult& result)
	{
		if (detail::SimulateExhaustedWorldRevision)
		{
			return { ValidationError::InvalidVoxelMutation };
		}

		detail::SimulateExhaustedWorldRevision = true;
		const ValidationResult mutationResult =
			napa::voxel::ApplySphereEdit(world, request, result);
		detail::SimulateExhaustedWorldRevision = false;
		return mutationResult;
	}

	ValidationResult RestoreAllWithAllocationProbe(
		VoxelWorld& world, VoxelMutationResult& result, VoxelRestoreAllocationProbe& probe)
	{
		if (detail::ActiveOperationAllocationProbe != nullptr)
		{
			return { ValidationError::InvalidVoxelMutation };
		}

		probe.m_PrepareAllocationCount = 0;
		probe.m_CommitAllocationCount = 0;
		detail::IsVoxelOperationCommitPhase = false;
		detail::ActiveOperationAllocationProbe = &probe;
		const ValidationResult restoreResult = napa::voxel::RestoreAll(world, result);
		detail::ActiveOperationAllocationProbe = nullptr;
		detail::IsVoxelOperationCommitPhase = false;
		return restoreResult;
	}

	ValidationResult RestoreAllWithExhaustedRevision(
		VoxelWorld& world, VoxelMutationResult& result)
	{
		if (detail::SimulateExhaustedWorldRevision)
		{
			return { ValidationError::InvalidVoxelMutation };
		}

		detail::SimulateExhaustedWorldRevision = true;
		const ValidationResult restoreResult = napa::voxel::RestoreAll(world, result);
		detail::SimulateExhaustedWorldRevision = false;
		return restoreResult;
	}

	ValidationResult PrepareWithAuthoritativeRevision(
		const VoxelWorld& authoritativeWorld, const VoxelMutationResult& mutation,
		const VisibleMeshSet& visible, std::uint64_t authoritativeRevision,
		std::unique_ptr<PendingDataOnlyPublication>& pending)
	{
		if (detail::SimulatedAuthoritativeRevision != 0 || authoritativeRevision == 0)
		{
			return { ValidationError::InvalidDataOnlyPublication };
		}
		detail::SimulatedAuthoritativeRevision = authoritativeRevision;
		const ValidationResult result = PrepareDataOnlyPublication(
			authoritativeWorld, mutation, visible, pending);
		detail::SimulatedAuthoritativeRevision = 0;
		return result;
	}

	ValidationResult PrepareWithAllocationFailure(
		const VoxelWorld& authoritativeWorld, const VoxelMutationResult& mutation,
		const VisibleMeshSet& visible,
		std::unique_ptr<PendingDataOnlyPublication>& pending)
	{
		if (detail::SimulatePublicationAllocationFailure)
		{
			return { ValidationError::InvalidDataOnlyPublication };
		}
		detail::SimulatePublicationAllocationFailure = true;
		const ValidationResult result = PrepareDataOnlyPublication(
			authoritativeWorld, mutation, visible, pending);
		detail::SimulatePublicationAllocationFailure = false;
		return result;
	}
}
