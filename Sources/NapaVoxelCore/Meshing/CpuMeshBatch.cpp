#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"

#include "NapaVoxelCore/Edit/VoxelMutation.h"
#include "NapaVoxelCore/Meshing/ReferenceMesher.h"
#include "NapaVoxelCore/Validation/CheckedArithmetic.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace napa::voxel
{
	namespace
	{
		[[nodiscard]] ValidationResult ValidateRequestedChunks(
			std::span<const ChunkCoord> requestedChunks, const VoxelWorldConfig& config) noexcept
		{
			LogicalDomainMetrics metrics{};
			const ValidationResult metricsResult = ComputeLogicalDomainMetrics(config, metrics);
			if (metricsResult.Failed())
			{
				return metricsResult;
			}

			const ChunkCoordZYXLess chunkLess{};
			std::optional<ChunkCoord> previous;
			for (const ChunkCoord chunk : requestedChunks)
			{
				if (!metrics.m_CellOwnerChunkBounds.Contains(chunk) ||
					(previous && !chunkLess(*previous, chunk)))
				{
					return { ValidationError::InvalidCpuMeshCandidateSet };
				}
				previous = chunk;
			}
			return {};
		}

		[[nodiscard]] ValidationResult ValidateCandidateSet(
			const CpuMeshBatch& batch) noexcept
		{
			const ValidationResult requestedResult =
				ValidateRequestedChunks(batch.m_RequestedChunks, batch.m_Config);
			if (requestedResult.Failed())
			{
				return requestedResult;
			}
			if (batch.m_Candidates.size() != batch.m_RequestedChunks.size())
			{
				return { ValidationError::MismatchedCpuMeshCandidateSet };
			}

			for (std::size_t index = 0; index < batch.m_Candidates.size(); ++index)
			{
				const ChunkMeshRecord& candidate = batch.m_Candidates[index];
				if (candidate.m_Chunk != batch.m_RequestedChunks[index])
				{
					return { ValidationError::MismatchedCpuMeshCandidateSet };
				}
				if (candidate.m_SourceWorldVoxelRevision != batch.m_TargetWorldVoxelRevision)
				{
					return { ValidationError::MismatchedCpuMeshSourceRevision };
				}
			}
			return {};
		}

		[[nodiscard]] ValidationResult ValidateVisibleState(const VisibleMeshSet& visible)
		{
			if (!visible.HasPublishedMeshes())
			{
				return {};
			}

			WorldMeshValidationResult worldValidation{};
			const ValidationResult worldResult = ValidateAndHashWorldMeshRecords(
				visible.GetChunks(), visible.GetConfig(), worldValidation);
			if (worldResult.Failed())
			{
				return worldResult;
			}
			if (worldValidation != visible.GetWorldMeshValidation())
			{
				return { ValidationError::MismatchedChunkMeshValidation };
			}

			BoundaryContourValidationResult boundaryValidation{};
			const ValidationResult boundaryResult = ValidateBoundaryContourSet(
				visible.GetChunks(), visible.GetConfig(), boundaryValidation);
			if (boundaryResult.Failed())
			{
				return boundaryResult;
			}
			if (boundaryValidation != visible.GetBoundaryValidation())
			{
				return { ValidationError::MismatchedBoundaryContour };
			}
			return {};
		}

		[[nodiscard]] ValidationResult MergeCandidates(
			const CpuMeshBatch& batch, const VisibleMeshSet& visible,
			std::vector<ChunkMeshRecord>& chunks)
		{
			if (!visible.HasPublishedMeshes())
			{
				chunks = batch.m_Candidates;
				return {};
			}

			chunks.assign(visible.GetChunks().begin(), visible.GetChunks().end());
			for (const ChunkMeshRecord& candidate : batch.m_Candidates)
			{
				const auto iterator = std::lower_bound(chunks.begin(), chunks.end(), candidate.m_Chunk,
					[](const ChunkMeshRecord& record, ChunkCoord coordinate)
					{ return ChunkCoordZYXLess{}(record.m_Chunk, coordinate); });
				if (iterator == chunks.end() || iterator->m_Chunk != candidate.m_Chunk)
				{
					return { ValidationError::MismatchedCpuMeshCandidateSet };
				}
				*iterator = candidate;
			}
			return {};
		}

		[[nodiscard]] ValidationResult BuildCpuMeshBatchFromValidatedRequest(
			const VoxelWorld& world, std::uint64_t targetWorldVoxelRevision,
			std::span<const ChunkCoord> requestedChunks, CpuMeshBatch& batch)
		{
			CpuMeshBatch prepared{
				.m_Config = world.GetConfig(),
				.m_TargetWorldVoxelRevision = targetWorldVoxelRevision,
			};
			prepared.m_RequestedChunks.assign(requestedChunks.begin(), requestedChunks.end());
			prepared.m_Candidates.reserve(requestedChunks.size());
			const ReferenceMesher mesher(world);
			for (const ChunkCoord chunk : requestedChunks)
			{
				ChunkMeshRecord candidate{};
				const ValidationResult meshResult = mesher.MeshChunk(chunk, candidate);
				if (meshResult.Failed())
				{
					return meshResult;
				}
				if (candidate.m_SourceWorldVoxelRevision != targetWorldVoxelRevision)
				{
					return { ValidationError::MismatchedCpuMeshSourceRevision };
				}
				prepared.m_Candidates.push_back(std::move(candidate));
			}
			if (world.GetWorldVoxelRevision() != targetWorldVoxelRevision)
			{
				return { ValidationError::MismatchedCpuMeshTargetRevision };
			}

			batch = std::move(prepared);
			return {};
		}
	}

	CpuMeshReplacementView::CpuMeshReplacementView(
		std::span<const ChunkMeshRecord> chunks,
		std::span<const std::size_t> indices) noexcept :
		m_Chunks(chunks),
		m_Indices(indices)
	{
	}

	std::size_t CpuMeshReplacementView::size() const noexcept
	{
		return m_Indices.size();
	}

	bool CpuMeshReplacementView::empty() const noexcept
	{
		return m_Indices.empty();
	}

	const ChunkMeshRecord& CpuMeshReplacementView::operator[](
		std::size_t index) const noexcept
	{
		return m_Chunks[m_Indices[index]];
	}

	PendingCpuMeshBatch::PendingCpuMeshBatch(ConstructionToken, State state) noexcept :
		m_State(std::move(state))
	{
	}

	const VoxelWorldConfig& PendingCpuMeshBatch::GetConfig() const noexcept
	{
		return m_State.m_Config;
	}

	std::uint64_t PendingCpuMeshBatch::GetTargetWorldVoxelRevision() const noexcept
	{
		return m_State.m_TargetWorldVoxelRevision;
	}

	std::uint64_t PendingCpuMeshBatch::GetCandidateChunkCount() const noexcept
	{
		return m_State.m_CandidateChunkCount;
	}

	std::span<const ChunkMeshRecord> PendingCpuMeshBatch::GetChunks() const noexcept
	{
		return m_State.m_Chunks;
	}

	CpuMeshReplacementView PendingCpuMeshBatch::GetReplacementChunks() const noexcept
	{
		return CpuMeshReplacementView{
			m_State.m_Chunks,
			m_State.m_ReplacementChunkIndices,
		};
	}

	const WorldMeshValidationResult& PendingCpuMeshBatch::GetWorldMeshValidation() const noexcept
	{
		return m_State.m_WorldMeshValidation;
	}

	const BoundaryContourValidationResult&
		PendingCpuMeshBatch::GetBoundaryValidation() const noexcept
	{
		return m_State.m_BoundaryValidation;
	}

	bool VisibleMeshSet::HasPublishedMeshes() const noexcept
	{
		return m_State.m_HasPublishedMeshes;
	}

	const VoxelWorldConfig& VisibleMeshSet::GetConfig() const noexcept
	{
		return m_State.m_Config;
	}

	std::uint64_t VisibleMeshSet::GetVisibleWorldRevision() const noexcept
	{
		return m_State.m_VisibleWorldRevision;
	}

	std::span<const ChunkMeshRecord> VisibleMeshSet::GetChunks() const noexcept
	{
		return m_State.m_Chunks;
	}

	const WorldMeshValidationResult& VisibleMeshSet::GetWorldMeshValidation() const noexcept
	{
		return m_State.m_WorldMeshValidation;
	}

	const BoundaryContourValidationResult& VisibleMeshSet::GetBoundaryValidation() const noexcept
	{
		return m_State.m_BoundaryValidation;
	}

	ValidationResult BuildCpuMeshBatch(const VoxelWorld& world,
		std::uint64_t targetWorldVoxelRevision, std::span<const ChunkCoord> requestedChunks,
		CpuMeshBatch& batch)
	{
		const VoxelWorldConfig& config = world.GetConfig();
		const ValidationResult configResult = ValidateConfig(config);
		if (configResult.Failed())
		{
			return configResult;
		}
		if (targetWorldVoxelRevision == 0 ||
			targetWorldVoxelRevision != world.GetWorldVoxelRevision())
		{
			return { ValidationError::MismatchedCpuMeshTargetRevision };
		}
		const ValidationResult requestedResult =
			ValidateRequestedChunks(requestedChunks, config);
		if (requestedResult.Failed())
		{
			return requestedResult;
		}

		return BuildCpuMeshBatchFromValidatedRequest(
			world, targetWorldVoxelRevision, requestedChunks, batch);
	}

	ValidationResult BuildCpuMeshBatch(const VoxelWorld& world,
		const VoxelMutationResult& mutation, CpuMeshBatch& batch)
	{
		const auto expectedTarget = CheckedAdd(
			mutation.m_BaseWorldVoxelRevision, std::uint64_t{ 1 });
		if (!mutation.Changed() || !expectedTarget.has_value() ||
			*expectedTarget != mutation.m_TargetWorldVoxelRevision)
		{
			return { ValidationError::InvalidVoxelMutation };
		}
		if (mutation.m_TargetWorldVoxelRevision != world.GetWorldVoxelRevision())
		{
			return { ValidationError::MismatchedCpuMeshTargetRevision };
		}

		std::vector<ChunkCoord> dataDirtyChunks;
		std::vector<ChunkCoord> meshDirtyChunks;
		const ValidationResult dirtyResult = DeriveVoxelMutationDirtyChunks(
			world.GetConfig(), mutation.m_SampleChanges, dataDirtyChunks, meshDirtyChunks);
		if (dirtyResult.Failed())
		{
			return dirtyResult;
		}
		if (dataDirtyChunks != mutation.m_DataDirtyChunks ||
			meshDirtyChunks != mutation.m_MeshDirtyChunks)
		{
			return { ValidationError::InvalidVoxelMutation };
		}

		return BuildCpuMeshBatchFromValidatedRequest(
			world, mutation.m_TargetWorldVoxelRevision, meshDirtyChunks, batch);
	}

	ValidationResult ValidateCpuMeshBatch(const CpuMeshBatch& batch,
		const VisibleMeshSet& visible, std::unique_ptr<PendingCpuMeshBatch>& pending)
	{
		const ValidationResult configResult = ValidateConfig(batch.m_Config);
		if (configResult.Failed())
		{
			return configResult;
		}
		if (batch.m_TargetWorldVoxelRevision == 0)
		{
			return { ValidationError::MismatchedCpuMeshTargetRevision };
		}
		const ValidationResult candidateResult = ValidateCandidateSet(batch);
		if (candidateResult.Failed())
		{
			return candidateResult;
		}
		const ValidationResult visibleResult = ValidateVisibleState(visible);
		if (visibleResult.Failed())
		{
			return visibleResult;
		}
		if (visible.m_State.m_HasPublishedMeshes &&
			batch.m_Config != visible.m_State.m_Config)
		{
			return { ValidationError::MismatchedCpuMeshConfig };
		}
		if (visible.m_State.m_HasPublishedMeshes &&
			batch.m_TargetWorldVoxelRevision <= visible.m_State.m_VisibleWorldRevision)
		{
			return { ValidationError::StaleCpuMeshBatch };
		}

		std::vector<ChunkMeshRecord> chunks;
		const ValidationResult mergeResult = MergeCandidates(batch, visible, chunks);
		if (mergeResult.Failed())
		{
			return mergeResult;
		}

		WorldMeshValidationResult worldValidation{};
		const ValidationResult worldResult =
			ValidateAndHashWorldMeshRecords(chunks, batch.m_Config, worldValidation);
		if (worldResult.Failed())
		{
			return worldResult;
		}
		BoundaryContourValidationResult boundaryValidation{};
		const ValidationResult boundaryResult =
			ValidateBoundaryContourSet(chunks, batch.m_Config, boundaryValidation);
		if (boundaryResult.Failed())
		{
			return boundaryResult;
		}

		const std::optional<std::uint64_t> candidateChunkCount =
			CheckedNarrow<std::uint64_t>(batch.m_Candidates.size());
		if (!candidateChunkCount)
		{
			return { ValidationError::ArithmeticOverflow };
		}
		PendingCpuMeshBatch::State state{
			.m_BaseWasPublished = visible.m_State.m_HasPublishedMeshes,
			.m_BaseVisibleWorldRevision = visible.m_State.m_VisibleWorldRevision,
			.m_BaseWorldMeshValidation = visible.m_State.m_WorldMeshValidation,
			.m_BaseBoundaryValidation = visible.m_State.m_BoundaryValidation,
			.m_Config = batch.m_Config,
			.m_TargetWorldVoxelRevision = batch.m_TargetWorldVoxelRevision,
			.m_CandidateChunkCount = *candidateChunkCount,
			.m_Chunks = std::move(chunks),
			.m_WorldMeshValidation = worldValidation,
			.m_BoundaryValidation = boundaryValidation,
		};
		std::unique_ptr<PendingCpuMeshBatch> prepared =
			std::make_unique<PendingCpuMeshBatch>(
				PendingCpuMeshBatch::ConstructionToken{}, std::move(state));
		prepared->m_State.m_ReplacementChunkIndices.reserve(batch.m_RequestedChunks.size());
		for (const ChunkCoord chunk : batch.m_RequestedChunks)
		{
			const auto iterator = std::lower_bound(
				prepared->m_State.m_Chunks.begin(), prepared->m_State.m_Chunks.end(), chunk,
				[](const ChunkMeshRecord& record, ChunkCoord coordinate) noexcept
				{ return ChunkCoordZYXLess{}(record.m_Chunk, coordinate); });
			if (iterator == prepared->m_State.m_Chunks.end() || iterator->m_Chunk != chunk)
			{
				return { ValidationError::MismatchedCpuMeshCandidateSet };
			}
			prepared->m_State.m_ReplacementChunkIndices.push_back(
				static_cast<std::size_t>(iterator - prepared->m_State.m_Chunks.begin()));
		}
		pending = std::move(prepared);
		return {};
	}

	ValidationResult PrepareCpuMeshBatchPublication(
		std::unique_ptr<PendingCpuMeshBatch>& pending, const VisibleMeshSet& visible,
		std::unique_ptr<PreparedCpuMeshPublication>& publication)
	{
		if (!pending)
		{
			return { ValidationError::InvalidCpuMeshCandidateSet };
		}
		const PendingCpuMeshBatch::State& pendingState = pending->m_State;
		if (pendingState.m_TargetWorldVoxelRevision <=
			visible.m_State.m_VisibleWorldRevision)
		{
			return { ValidationError::StaleCpuMeshBatch };
		}
		const bool baseMatches =
			pendingState.m_BaseWasPublished == visible.m_State.m_HasPublishedMeshes &&
			(!pendingState.m_BaseWasPublished ||
				(pendingState.m_BaseVisibleWorldRevision ==
					visible.m_State.m_VisibleWorldRevision &&
					pendingState.m_BaseWorldMeshValidation ==
					visible.m_State.m_WorldMeshValidation &&
					pendingState.m_BaseBoundaryValidation ==
					visible.m_State.m_BoundaryValidation));
		if (!baseMatches)
		{
			return { ValidationError::StaleCpuMeshBatch };
		}

		std::unique_ptr<PreparedCpuMeshPublication> prepared =
			std::make_unique<PreparedCpuMeshPublication>(
				PreparedCpuMeshPublication::ConstructionToken{});
		prepared->m_Pending = std::move(pending);
		publication = std::move(prepared);
		return {};
	}

	void CommitCpuMeshBatchPublication(
		std::unique_ptr<PreparedCpuMeshPublication>& publication, VisibleMeshSet& visible) noexcept
	{
		if (!publication || !publication->m_Pending)
		{
			return;
		}

		std::unique_ptr<PendingCpuMeshBatch> pending =
			std::move(publication->m_Pending);
		const PendingCpuMeshBatch::State& pendingState = pending->m_State;
		VisibleMeshSet::State published{
			.m_HasPublishedMeshes = true,
			.m_Config = pendingState.m_Config,
			.m_VisibleWorldRevision = pendingState.m_TargetWorldVoxelRevision,
			.m_Chunks = std::move(pending->m_State.m_Chunks),
			.m_WorldMeshValidation = pendingState.m_WorldMeshValidation,
			.m_BoundaryValidation = pendingState.m_BoundaryValidation,
		};
		visible.m_State = std::move(published);
		publication.reset();
	}

	ValidationResult ComputeVisibleWorldMeshHash(
		const VisibleMeshSet& visible, WorldMeshValidationResult& result)
	{
		if (!visible.m_State.m_HasPublishedMeshes)
		{
			return { ValidationError::VisibleMeshSetUninitialized };
		}

		WorldMeshValidationResult validated{};
		const ValidationResult validationResult = ValidateAndHashWorldMeshRecords(
			visible.m_State.m_Chunks, visible.m_State.m_Config, validated);
		if (validationResult.Failed())
		{
			return validationResult;
		}
		if (validated != visible.m_State.m_WorldMeshValidation)
		{
			return { ValidationError::MismatchedChunkMeshValidation };
		}
		result = validated;
		return {};
	}
}
