#include "NapaVoxelCore/Meshing/DataOnlyPublication.h"

#include "NapaVoxelCore/Meshing/DataOnlyPublicationDetail.h"

#include "NapaVoxelCore/Edit/VoxelMutation.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <algorithm>
#include <memory>
#include <new>
#include <vector>

namespace napa::voxel
{
	namespace detail
	{
		thread_local std::uint64_t SimulatedAuthoritativeRevision = 0;
		thread_local bool SimulatePublicationAllocationFailure = false;
	}

	PendingDataOnlyPublication::PendingDataOnlyPublication(
		ConstructionToken, std::uint64_t targetWorldRevision) noexcept :
		m_TargetWorldRevision(targetWorldRevision)
	{
	}

	std::uint64_t PendingDataOnlyPublication::GetTargetWorldRevision() const noexcept
	{
		return m_TargetWorldRevision;
	}

	ValidationResult PrepareDataOnlyPublication(
		const VoxelWorld& authoritativeWorld, const VoxelMutationResult& mutation,
		const VisibleMeshSet& visible,
		std::unique_ptr<PendingDataOnlyPublication>& pending)
	{
		if (!visible.HasPublishedMeshes() || mutation.m_SampleChanges.empty() ||
			authoritativeWorld.GetConfig() != visible.GetConfig() ||
			visible.GetSurfaceStateRevision() == 0 ||
			visible.GetSurfaceStateRevision() > visible.GetVisibleWorldRevision() ||
			authoritativeWorld.GetSurfaceStateRevision() == 0 ||
			authoritativeWorld.GetSurfaceStateRevision() >
			authoritativeWorld.GetWorldVoxelRevision())
		{
			return { ValidationError::InvalidDataOnlyPublication };
		}
		if (mutation.m_BaseWorldVoxelRevision != visible.GetVisibleWorldRevision() ||
			mutation.m_TargetWorldVoxelRevision <= visible.GetVisibleWorldRevision())
		{
			return { ValidationError::StaleDataOnlyPublication };
		}
		const std::uint64_t authoritativeRevision = detail::SimulatedAuthoritativeRevision != 0
			? detail::SimulatedAuthoritativeRevision
			: authoritativeWorld.GetWorldVoxelRevision();
		if (mutation.m_TargetWorldVoxelRevision != authoritativeRevision ||
			!mutation.m_MeshDirtyChunks.empty() ||
			authoritativeWorld.GetSurfaceStateRevision() !=
			visible.GetSurfaceStateRevision())
		{
			return { ValidationError::MismatchedDataOnlyPublication };
		}

		SampleCoordZYXLess sampleLess{};
		for (std::size_t index = 0; index < mutation.m_SampleChanges.size(); ++index)
		{
			const VoxelSampleChange& change = mutation.m_SampleChanges[index];
			if (change.m_Before == change.m_After ||
				change.m_Before.m_Density != change.m_After.m_Density ||
				change.m_Before.m_Material != change.m_After.m_Material ||
				change.m_Before.m_Damage == change.m_After.m_Damage ||
				(index != 0 && !sampleLess(
					mutation.m_SampleChanges[index - 1].m_Coordinate,
					change.m_Coordinate)))
			{
				return { ValidationError::InvalidDataOnlyPublication };
			}

			VoxelSample authoritativeSample{};
			const ValidationResult readResult = authoritativeWorld.ReadCurrentSample(
				change.m_Coordinate, authoritativeSample);
			if (readResult.Failed())
			{
				return readResult;
			}
			if (authoritativeSample != change.m_After)
			{
				return { ValidationError::MismatchedDataOnlyPublication };
			}
		}

		std::vector<ChunkCoord> expectedDataDirtyChunks;
		std::vector<ChunkCoord> expectedMeshDirtyChunks;
		const ValidationResult dirtyResult = DeriveVoxelMutationDirtyChunks(
			authoritativeWorld.GetConfig(), mutation.m_SampleChanges,
			expectedDataDirtyChunks, expectedMeshDirtyChunks);
		if (dirtyResult.Failed())
		{
			return dirtyResult;
		}
		if (!expectedMeshDirtyChunks.empty() ||
			expectedDataDirtyChunks != mutation.m_DataDirtyChunks)
		{
			return { ValidationError::MismatchedDataOnlyPublication };
		}

		if (detail::SimulatePublicationAllocationFailure)
		{
			return { ValidationError::DataOnlyPublicationAllocationFailure };
		}

		auto prepared = std::unique_ptr<PendingDataOnlyPublication>(
			new (std::nothrow) PendingDataOnlyPublication(
				PendingDataOnlyPublication::ConstructionToken{},
				mutation.m_TargetWorldVoxelRevision));
		if (!prepared)
		{
			return { ValidationError::DataOnlyPublicationAllocationFailure };
		}
		pending = std::move(prepared);
		return {};
	}

	void CommitDataOnlyPublication(
		std::unique_ptr<PendingDataOnlyPublication>& pending,
		VisibleMeshSet& visible) noexcept
	{
		visible.m_State.m_VisibleWorldRevision = pending->m_TargetWorldRevision;
		pending.reset();
	}


}
