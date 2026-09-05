#include "NapaVoxelCore/World/VoxelRestore.h"

#include "NapaVoxelCore/Edit/VoxelOperationDetail.h"
#include "NapaVoxelCore/Validation/CheckedArithmetic.h"
#include "NapaVoxelCore/World/VoxelChunk.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace napa::voxel
{
	namespace
	{
		struct PreparedRestoreWrite
		{
			VoxelChunk* m_Chunk = nullptr;
			std::size_t m_FlatIndex = 0;
			VoxelSample m_After{};
		};

		[[nodiscard]] SampleAabb IntersectSampleOwnerChunk(ChunkCoord chunk,
			std::uint32_t chunkCellCount, const SampleAabb& logicalBounds) noexcept
		{
			const std::int64_t chunkSize = chunkCellCount;
			const std::int64_t minimumX = static_cast<std::int64_t>(chunk.m_X) * chunkSize;
			const std::int64_t minimumY = static_cast<std::int64_t>(chunk.m_Y) * chunkSize;
			const std::int64_t minimumZ = static_cast<std::int64_t>(chunk.m_Z) * chunkSize;
			const std::int64_t maximumX = minimumX + chunkSize;
			const std::int64_t maximumY = minimumY + chunkSize;
			const std::int64_t maximumZ = minimumZ + chunkSize;

			return {
				.m_Min = {
					static_cast<std::int32_t>(std::max(
						minimumX, static_cast<std::int64_t>(logicalBounds.m_Min.m_X))),
					static_cast<std::int32_t>(std::max(
						minimumY, static_cast<std::int64_t>(logicalBounds.m_Min.m_Y))),
					static_cast<std::int32_t>(std::max(
						minimumZ, static_cast<std::int64_t>(logicalBounds.m_Min.m_Z))),
				},
				.m_MaxExclusive = {
					static_cast<std::int32_t>(std::min(maximumX,
						static_cast<std::int64_t>(logicalBounds.m_MaxExclusive.m_X))),
					static_cast<std::int32_t>(std::min(maximumY,
						static_cast<std::int64_t>(logicalBounds.m_MaxExclusive.m_Y))),
					static_cast<std::int32_t>(std::min(maximumZ,
						static_cast<std::int64_t>(logicalBounds.m_MaxExclusive.m_Z))),
				},
			};
		}

		[[nodiscard]] SampleAabb IntersectSampleBounds(
			const SampleAabb& first, const SampleAabb& second) noexcept
		{
			return {
				.m_Min = {
					std::max(first.m_Min.m_X, second.m_Min.m_X),
					std::max(first.m_Min.m_Y, second.m_Min.m_Y),
					std::max(first.m_Min.m_Z, second.m_Min.m_Z),
				},
				.m_MaxExclusive = {
					std::min(first.m_MaxExclusive.m_X, second.m_MaxExclusive.m_X),
					std::min(first.m_MaxExclusive.m_Y, second.m_MaxExclusive.m_Y),
					std::min(first.m_MaxExclusive.m_Z, second.m_MaxExclusive.m_Z),
				},
			};
		}
	}

	ValidationResult RestoreAll(VoxelWorld& world, VoxelMutationResult& result)
	{
		return world.RestoreRegionInternal(world.m_LogicalSampleBounds, result);
	}

	ValidationResult RestoreSampleOwnerChunk(
		VoxelWorld& world, ChunkCoord chunk, VoxelMutationResult& result)
	{
		if (!world.m_LogicalDomainMetrics.m_SampleOwnerChunkBounds.Contains(chunk))
		{
			return { ValidationError::ChunkOutsideLogicalSampleDomain };
		}

		const SampleAabb region = IntersectSampleOwnerChunk(
			chunk, world.m_Config.m_ChunkCellCount, world.m_LogicalSampleBounds);
		return world.RestoreRegionInternal(region, result);
	}

	ValidationResult RestoreRegion(
		VoxelWorld& world, const SampleAabb& region, VoxelMutationResult& result)
	{
		if (region.IsEmpty())
		{
			return { ValidationError::EmptySampleBounds };
		}
		if (!world.m_LogicalSampleBounds.ContainsBounds(region))
		{
			return { ValidationError::SampleOutsideLogicalBounds };
		}

		return world.RestoreRegionInternal(region, result);
	}

	ValidationResult VoxelWorld::RestoreRegionInternal(
		const SampleAabb& region, VoxelMutationResult& result)
	{
		static_assert(std::is_nothrow_assignable_v<VoxelSample&, const VoxelSample&>);
		static_assert(std::is_nothrow_assignable_v<std::uint64_t&, std::uint64_t>);
		static_assert(std::is_nothrow_swappable_v<std::vector<VoxelSampleChange>>);
		static_assert(std::is_nothrow_swappable_v<std::vector<ChunkCoord>>);

		VoxelMutationResult preparedResult{
			.m_BaseWorldVoxelRevision = m_WorldVoxelRevision,
			.m_TargetWorldVoxelRevision = m_WorldVoxelRevision,
		};
		std::vector<PreparedRestoreWrite> preparedWrites;
		const auto commitRestore = [&preparedWrites, &preparedResult, this, &result]() noexcept
			{
				for (const PreparedRestoreWrite& write : preparedWrites)
				{
					write.m_Chunk->m_CurrentSamples[write.m_FlatIndex] = write.m_After;
					write.m_Chunk->m_VoxelRevision = preparedResult.m_TargetWorldVoxelRevision;
				}

				m_WorldVoxelRevision = preparedResult.m_TargetWorldVoxelRevision;
				if (!preparedResult.m_MeshDirtyChunks.empty())
				{
					m_SurfaceStateRevision = preparedResult.m_TargetWorldVoxelRevision;
				}
				result.m_BaseWorldVoxelRevision = preparedResult.m_BaseWorldVoxelRevision;
				result.m_TargetWorldVoxelRevision = preparedResult.m_TargetWorldVoxelRevision;
				result.m_SampleChanges.swap(preparedResult.m_SampleChanges);
				result.m_DataDirtyChunks.swap(preparedResult.m_DataDirtyChunks);
				result.m_MeshDirtyChunks.swap(preparedResult.m_MeshDirtyChunks);
			};

		for (const auto& [chunkCoordinate, chunkOwner] : m_Chunks)
		{
			VoxelChunk* const chunk = chunkOwner.get();
			const SampleAabb chunkBounds = IntersectSampleOwnerChunk(
				chunkCoordinate, m_Config.m_ChunkCellCount, m_LogicalSampleBounds);
			const SampleAabb scanBounds = IntersectSampleBounds(chunkBounds, region);
			if (scanBounds.IsEmpty())
			{
				continue;
			}

			for (std::int64_t z = scanBounds.m_Min.m_Z;
				z < scanBounds.m_MaxExclusive.m_Z; ++z)
			{
				for (std::int64_t y = scanBounds.m_Min.m_Y;
					y < scanBounds.m_MaxExclusive.m_Y; ++y)
				{
					for (std::int64_t x = scanBounds.m_Min.m_X;
						x < scanBounds.m_MaxExclusive.m_X; ++x)
					{
						const SampleCoord coordinate{
							static_cast<std::int32_t>(x),
							static_cast<std::int32_t>(y),
							static_cast<std::int32_t>(z),
						};
						OwnedSampleAddress address{};
						const ValidationResult addressResult = ResolveSampleOwner(
							coordinate, m_Config.m_ChunkCellCount, address);
						if (addressResult.Failed())
						{
							return addressResult;
						}
						if (address.m_Owner != chunkCoordinate)
						{
							return { ValidationError::InvalidVoxelMutation };
						}

						std::size_t flatIndex = 0;
						const ValidationResult indexResult = FlattenLocal(
							address.m_Local, m_Config.m_ChunkCellCount, flatIndex);
						if (indexResult.Failed())
						{
							return indexResult;
						}
						if (flatIndex >= chunk->m_CurrentSamples.size() ||
							flatIndex >= chunk->m_OriginalSamples.size())
						{
							return { ValidationError::InvalidVoxelMutation };
						}

						const VoxelSample before = chunk->m_CurrentSamples[flatIndex];
						const VoxelSample after = chunk->m_OriginalSamples[flatIndex];
						if (before == after)
						{
							continue;
						}

						const ValidationResult changeAppendResult = detail::AppendTracked(
							preparedResult.m_SampleChanges, VoxelSampleChange{
							.m_Coordinate = coordinate,
							.m_Before = before,
							.m_After = after,
							});
						if (changeAppendResult.Failed())
						{
							return changeAppendResult;
						}
						const ValidationResult writeAppendResult = detail::AppendTracked(
							preparedWrites, PreparedRestoreWrite{
							.m_Chunk = chunk,
							.m_FlatIndex = flatIndex,
							.m_After = after,
							});
						if (writeAppendResult.Failed())
						{
							return writeAppendResult;
						}
					}
				}
			}
		}

		std::sort(preparedResult.m_SampleChanges.begin(),
			preparedResult.m_SampleChanges.end(), [](const VoxelSampleChange& first,
				const VoxelSampleChange& second) noexcept
			{
				return std::tie(first.m_Coordinate.m_Z, first.m_Coordinate.m_Y,
					first.m_Coordinate.m_X) < std::tie(second.m_Coordinate.m_Z,
						second.m_Coordinate.m_Y, second.m_Coordinate.m_X);
			});

		const ValidationResult dirtyResult = detail::DeriveDirtyChunksFromValidatedConfig(
			m_Config, preparedResult.m_SampleChanges,
			preparedResult.m_DataDirtyChunks, preparedResult.m_MeshDirtyChunks, false);
		if (dirtyResult.Failed())
		{
			return dirtyResult;
		}

		if (preparedResult.Changed())
		{
			const std::uint64_t revisionForTarget = detail::SimulateExhaustedWorldRevision ?
				std::numeric_limits<std::uint64_t>::max() : m_WorldVoxelRevision;
			const auto targetRevision = CheckedAdd(revisionForTarget, std::uint64_t{ 1 });
			if (!targetRevision.has_value())
			{
				return { ValidationError::ArithmeticOverflow };
			}
			preparedResult.m_TargetWorldVoxelRevision = *targetRevision;
		}

		detail::IsVoxelOperationCommitPhase = true;
		commitRestore();
		detail::IsVoxelOperationCommitPhase = false;
		return {};
	}


}
