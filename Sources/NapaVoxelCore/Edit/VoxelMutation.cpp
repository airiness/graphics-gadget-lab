#include "NapaVoxelCore/Edit/VoxelMutation.h"

#include "NapaVoxelCore/Edit/VoxelOperationDetail.h"
#include "NapaVoxelCore/Testing/VoxelMutationTestAccess.h"
#include "NapaVoxelCore/World/VoxelChunk.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace napa::voxel
{
	namespace detail
	{
		thread_local testing::VoxelOperationAllocationProbe* ActiveOperationAllocationProbe = nullptr;
		thread_local bool SimulateExhaustedWorldRevision = false;
		thread_local bool IsVoxelOperationCommitPhase = false;

		ValidationResult RecordPotentialAllocation() noexcept
		{
			if (ActiveOperationAllocationProbe == nullptr)
			{
				return {};
			}

			std::size_t& count = IsVoxelOperationCommitPhase ?
				ActiveOperationAllocationProbe->m_CommitAllocationCount :
				ActiveOperationAllocationProbe->m_PrepareAllocationCount;
			const auto nextCount = CheckedAdd(count, std::size_t{ 1 });
			if (!nextCount.has_value())
			{
				return { ValidationError::ArithmeticOverflow };
			}
			count = *nextCount;

			if (!IsVoxelOperationCommitPhase &&
				ActiveOperationAllocationProbe->m_FailAtPrepareAllocation == count)
			{
				return { ValidationError::VoxelMutationAllocationFailure };
			}
			return {};
		}
	}

	namespace
	{
		using detail::AppendTracked;
		using detail::RecordPotentialAllocation;

		struct PreparedSampleWrite
		{
			VoxelChunk* m_Chunk = nullptr;
			std::size_t m_FlatIndex = 0;
			VoxelSample m_After{};
		};

		void SortAndUniqueChunks(std::vector<ChunkCoord>& chunks) noexcept
		{
			const ChunkCoordZYXLess less{};
			std::sort(chunks.begin(), chunks.end(), less);
			chunks.erase(std::unique(chunks.begin(), chunks.end()), chunks.end());
		}

		[[nodiscard]] ValidationResult EvaluateSampleTransition(
			const SphereEditRequest& request,
			const SphereEditSampleEvaluation& evaluation,
			VoxelSample before, VoxelSample& after) noexcept
		{
			const ValidationResult validationResult = ValidateVoxelSample(before);
			if (validationResult.Failed())
			{
				return validationResult;
			}

			const auto applyDensitySubtract = [&request, &evaluation](VoxelSample& sample) noexcept
				{
					const std::int32_t currentSigned =
						static_cast<std::int32_t>(sample.m_Density) -
						static_cast<std::int32_t>(IsoValue);
					const std::int32_t brushSigned =
						static_cast<std::int32_t>(evaluation.m_BrushDensity) -
						static_cast<std::int32_t>(IsoValue);
					const std::int32_t targetSigned = std::min(currentSigned, -brushSigned);

					std::int32_t resultSigned = currentSigned;
					if (request.m_Brush.m_Strength == 1.0)
					{
						resultSigned = targetSigned;
					}
					else if (request.m_Brush.m_Strength > 0.0)
					{
						const double interpolated = static_cast<double>(currentSigned) +
							static_cast<double>(targetSigned - currentSigned) *
							request.m_Brush.m_Strength;
						std::int64_t rounded = 0;
						const ValidationResult roundingResult =
							RoundHalfAwayFromZero(interpolated, rounded);
						if (roundingResult.Failed())
						{
							return roundingResult;
						}
						resultSigned = static_cast<std::int32_t>(rounded);
					}

					resultSigned = std::clamp(resultSigned, -128, 127);
					sample.m_Density = static_cast<std::uint8_t>(resultSigned + IsoValue);
					return ValidationResult{};
				};

			VoxelSample prepared = before;
			if (before.m_Material == VoxelMaterial::Soil && evaluation.m_DensityPathEligible)
			{
				const ValidationResult subtractResult = applyDensitySubtract(prepared);
				if (subtractResult.Failed())
				{
					return subtractResult;
				}
			}
			else if (before.m_Material == VoxelMaterial::Stone &&
				evaluation.m_DamagePathEligible)
			{
				const std::uint16_t accumulatedDamage = std::min<std::uint16_t>(
					std::numeric_limits<std::uint8_t>::max(),
					static_cast<std::uint16_t>(before.m_Damage) +
					static_cast<std::uint16_t>(request.m_MaterialRules.m_DamagePerHit));
				prepared.m_Damage = static_cast<std::uint8_t>(accumulatedDamage);
				if (accumulatedDamage >= request.m_MaterialRules.m_StoneBreakThreshold)
				{
					const ValidationResult subtractResult = applyDensitySubtract(prepared);
					if (subtractResult.Failed())
					{
						return subtractResult;
					}
				}
			}

			VoxelSample storageSample{};
			const ValidationResult storageResult =
				PrepareVoxelSampleForStorage(prepared, storageSample);
			if (storageResult.Failed())
			{
				return storageResult;
			}

			after = storageSample;
			return {};
		}

	}

	namespace detail
	{
		ValidationResult DeriveDirtyChunksFromValidatedConfig(
			const VoxelWorldConfig& config, std::span<const VoxelSampleChange> changes,
			std::vector<ChunkCoord>& dataDirtyChunks,
			std::vector<ChunkCoord>& meshDirtyChunks, bool validateChanges)
		{
			SampleAabb logicalSampleBounds{};
			const ValidationResult boundsResult = LogicalCellBoundsToSampleBounds(
				config.m_LogicalCellBounds, logicalSampleBounds);
			if (boundsResult.Failed())
			{
				return boundsResult;
			}

			std::vector<ChunkCoord> preparedDataDirtyChunks;
			std::vector<ChunkCoord> preparedMeshDirtyChunks;
			for (const VoxelSampleChange& change : changes)
			{
				if (!logicalSampleBounds.Contains(change.m_Coordinate) ||
					change.m_Before == change.m_After)
				{
					return { ValidationError::InvalidVoxelSampleChange };
				}
				if (validateChanges)
				{
					const ValidationResult beforeResult = ValidateVoxelSample(change.m_Before);
					if (beforeResult.Failed())
					{
						return beforeResult;
					}
					const ValidationResult afterResult = ValidateVoxelSample(change.m_After);
					if (afterResult.Failed())
					{
						return afterResult;
					}
				}

				OwnedSampleAddress sampleAddress{};
				const ValidationResult sampleAddressResult = ResolveSampleOwner(
					change.m_Coordinate, config.m_ChunkCellCount, sampleAddress);
				if (sampleAddressResult.Failed())
				{
					return sampleAddressResult;
				}
				const ValidationResult dataAppendResult = AppendTracked(
					preparedDataDirtyChunks, sampleAddress.m_Owner);
				if (dataAppendResult.Failed())
				{
					return dataAppendResult;
				}

				const bool affectsMesh = change.m_Before.m_Density != change.m_After.m_Density ||
					change.m_Before.m_Material != change.m_After.m_Material;
				if (!affectsMesh)
				{
					continue;
				}

				for (std::int64_t zOffset = -1; zOffset <= 0; ++zOffset)
				{
					for (std::int64_t yOffset = -1; yOffset <= 0; ++yOffset)
					{
						for (std::int64_t xOffset = -1; xOffset <= 0; ++xOffset)
						{
							const std::int64_t x =
								static_cast<std::int64_t>(change.m_Coordinate.m_X) + xOffset;
							const std::int64_t y =
								static_cast<std::int64_t>(change.m_Coordinate.m_Y) + yOffset;
							const std::int64_t z =
								static_cast<std::int64_t>(change.m_Coordinate.m_Z) + zOffset;
							const CellAabb& bounds = config.m_LogicalCellBounds;
							if (x < bounds.m_Min.m_X || y < bounds.m_Min.m_Y ||
								z < bounds.m_Min.m_Z || x >= bounds.m_MaxExclusive.m_X ||
								y >= bounds.m_MaxExclusive.m_Y || z >= bounds.m_MaxExclusive.m_Z)
							{
								continue;
							}

							OwnedCellAddress cellAddress{};
							const CellCoord cell{
								static_cast<std::int32_t>(x),
								static_cast<std::int32_t>(y),
								static_cast<std::int32_t>(z),
							};
							const ValidationResult cellAddressResult = ResolveCellOwner(
								cell, config.m_ChunkCellCount, cellAddress);
							if (cellAddressResult.Failed())
							{
								return cellAddressResult;
							}
							const ValidationResult meshAppendResult = AppendTracked(
								preparedMeshDirtyChunks, cellAddress.m_Owner);
							if (meshAppendResult.Failed())
							{
								return meshAppendResult;
							}
						}
					}
				}
			}

			SortAndUniqueChunks(preparedDataDirtyChunks);
			SortAndUniqueChunks(preparedMeshDirtyChunks);
			dataDirtyChunks.swap(preparedDataDirtyChunks);
			meshDirtyChunks.swap(preparedMeshDirtyChunks);
			return {};
		}
	}

	namespace
	{
		using detail::DeriveDirtyChunksFromValidatedConfig;
	}

	ValidationResult DeriveVoxelMutationDirtyChunks(
		const VoxelWorldConfig& config, std::span<const VoxelSampleChange> changes,
		std::vector<ChunkCoord>& dataDirtyChunks,
		std::vector<ChunkCoord>& meshDirtyChunks)
	{
		const ValidationResult configResult = ValidateConfig(config);
		if (configResult.Failed())
		{
			return configResult;
		}
		return DeriveDirtyChunksFromValidatedConfig(
			config, changes, dataDirtyChunks, meshDirtyChunks, true);
	}

	ValidationResult EvaluateSphereEditSampleTransition(
		const SphereEditContext& context, SampleCoord sample,
		VoxelSample before, VoxelSample& after) noexcept
	{
		SphereEditSampleEvaluation evaluation{};
		const ValidationResult evaluationResult =
			EvaluateSphereEditSample(context, sample, evaluation);
		if (evaluationResult.Failed())
		{
			return evaluationResult;
		}

		VoxelSample prepared{};
		const ValidationResult transitionResult =
			EvaluateSampleTransition(context.m_Request, evaluation, before, prepared);
		if (transitionResult.Failed())
		{
			return transitionResult;
		}

		after = prepared;
		return {};
	}

	ValidationResult ApplySphereEdit(VoxelWorld& world,
		const SphereEditRequest& request, VoxelMutationResult& result)
	{
		using ChunkMap = VoxelWorld::ChunkMap;
		static_assert(std::allocator_traits<ChunkMap::allocator_type>::is_always_equal::value);
		static_assert(std::is_nothrow_invocable_r_v<bool, ChunkCoordZYXLess,
			ChunkCoord, ChunkCoord>);
		static_assert(std::is_nothrow_assignable_v<VoxelSample&, const VoxelSample&>);
		static_assert(std::is_nothrow_assignable_v<std::uint64_t&, std::uint64_t>);
		static_assert(std::is_nothrow_swappable_v<std::vector<VoxelSampleChange>>);
		static_assert(std::is_nothrow_swappable_v<std::vector<ChunkCoord>>);

		SphereEditContext editContext{};
		const ValidationResult contextResult =
			PrepareSphereEditContext(world.m_Config, request, editContext);
		if (contextResult.Failed())
		{
			return contextResult;
		}

		VoxelMutationResult preparedResult{
			.m_BaseWorldVoxelRevision = world.m_WorldVoxelRevision,
			.m_TargetWorldVoxelRevision = world.m_WorldVoxelRevision,
		};
		ChunkMap scratchChunks;
		std::vector<PreparedSampleWrite> preparedWrites;
		const auto commitMutation = [&scratchChunks, &preparedWrites, &preparedResult,
			&world, &result]() noexcept
			{
				world.m_Chunks.merge(scratchChunks);
				for (const PreparedSampleWrite& write : preparedWrites)
				{
					write.m_Chunk->m_CurrentSamples[write.m_FlatIndex] = write.m_After;
					write.m_Chunk->m_VoxelRevision = preparedResult.m_TargetWorldVoxelRevision;
				}

				world.m_WorldVoxelRevision = preparedResult.m_TargetWorldVoxelRevision;
				if (!preparedResult.m_MeshDirtyChunks.empty())
				{
					world.m_SurfaceStateRevision =
						preparedResult.m_TargetWorldVoxelRevision;
				}
				result.m_BaseWorldVoxelRevision = preparedResult.m_BaseWorldVoxelRevision;
				result.m_TargetWorldVoxelRevision = preparedResult.m_TargetWorldVoxelRevision;
				result.m_SampleChanges.swap(preparedResult.m_SampleChanges);
				result.m_DataDirtyChunks.swap(preparedResult.m_DataDirtyChunks);
				result.m_MeshDirtyChunks.swap(preparedResult.m_MeshDirtyChunks);
			};

		for (std::int64_t z = editContext.m_ScanBounds.m_Min.m_Z;
			z < editContext.m_ScanBounds.m_MaxExclusive.m_Z; ++z)
		{
			for (std::int64_t y = editContext.m_ScanBounds.m_Min.m_Y;
				y < editContext.m_ScanBounds.m_MaxExclusive.m_Y; ++y)
			{
				for (std::int64_t x = editContext.m_ScanBounds.m_Min.m_X;
					x < editContext.m_ScanBounds.m_MaxExclusive.m_X; ++x)
				{
					const SampleCoord coordinate{
						static_cast<std::int32_t>(x),
						static_cast<std::int32_t>(y),
						static_cast<std::int32_t>(z),
					};
					SphereEditSampleEvaluation evaluation{};
					const ValidationResult evaluationResult =
						EvaluateSphereEditSample(editContext, coordinate, evaluation);
					if (evaluationResult.Failed())
					{
						return evaluationResult;
					}
					if (!evaluation.m_DensityPathEligible &&
						!evaluation.m_DamagePathEligible)
					{
						continue;
					}

					OwnedSampleAddress address{};
					const ValidationResult addressResult = ResolveSampleOwner(
						coordinate, world.m_Config.m_ChunkCellCount, address);
					if (addressResult.Failed())
					{
						return addressResult;
					}
					std::size_t flatIndex = 0;
					const ValidationResult indexResult = FlattenLocal(
						address.m_Local, world.m_Config.m_ChunkCellCount, flatIndex);
					if (indexResult.Failed())
					{
						return indexResult;
					}

					VoxelChunk* chunk = nullptr;
					const auto worldChunk = world.m_Chunks.find(address.m_Owner);
					if (worldChunk != world.m_Chunks.end())
					{
						chunk = worldChunk->second.get();
						if (flatIndex >= chunk->m_CurrentSamples.size())
						{
							return { ValidationError::InvalidVoxelMutation };
						}
					}

					const VoxelSample before = chunk != nullptr ?
						chunk->m_CurrentSamples[flatIndex] : DefaultVoxelSample;
					VoxelSample after{};
					const ValidationResult transitionResult = EvaluateSampleTransition(
						editContext.m_Request, evaluation, before, after);
					if (transitionResult.Failed())
					{
						return transitionResult;
					}
					if (after == before)
					{
						continue;
					}

					if (chunk == nullptr)
					{
						auto scratchChunk = scratchChunks.find(address.m_Owner);
						if (scratchChunk == scratchChunks.end())
						{
							const ValidationResult chunkAllocationResult =
								RecordPotentialAllocation();
							if (chunkAllocationResult.Failed())
							{
								return chunkAllocationResult;
							}
							std::unique_ptr<VoxelChunk> created;
							const ValidationResult createResult = VoxelChunk::Create(
								world.m_Config.m_ChunkCellCount, created);
							if (createResult.Failed())
							{
								return createResult;
							}
							const ValidationResult mapAllocationResult =
								RecordPotentialAllocation();
							if (mapAllocationResult.Failed())
							{
								return mapAllocationResult;
							}
							const auto [iterator, inserted] = scratchChunks.try_emplace(
								address.m_Owner, std::move(created));
							if (!inserted)
							{
								return { ValidationError::InvalidVoxelMutation };
							}
							scratchChunk = iterator;
						}
						chunk = scratchChunk->second.get();
					}

					const ValidationResult changeAppendResult = AppendTracked(
						preparedResult.m_SampleChanges, VoxelSampleChange{
						.m_Coordinate = coordinate,
						.m_Before = before,
						.m_After = after,
						});
					if (changeAppendResult.Failed())
					{
						return changeAppendResult;
					}
					const ValidationResult writeAppendResult = AppendTracked(
						preparedWrites, PreparedSampleWrite{
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

		const ValidationResult dirtyResult = DeriveDirtyChunksFromValidatedConfig(
			world.m_Config, preparedResult.m_SampleChanges,
			preparedResult.m_DataDirtyChunks, preparedResult.m_MeshDirtyChunks, false);
		if (dirtyResult.Failed())
		{
			return dirtyResult;
		}

		if (!preparedResult.Changed())
		{
			detail::IsVoxelOperationCommitPhase = true;
			commitMutation();
			detail::IsVoxelOperationCommitPhase = false;
			return {};
		}

		const std::uint64_t revisionForTarget = detail::SimulateExhaustedWorldRevision ?
			std::numeric_limits<std::uint64_t>::max() : world.m_WorldVoxelRevision;
		const auto targetRevision = CheckedAdd(revisionForTarget, std::uint64_t{ 1 });
		if (!targetRevision.has_value())
		{
			return { ValidationError::ArithmeticOverflow };
		}
		preparedResult.m_TargetWorldVoxelRevision = *targetRevision;

		detail::IsVoxelOperationCommitPhase = true;
		commitMutation();
		detail::IsVoxelOperationCommitPhase = false;
		return {};
	}

	ValidationResult testing::VoxelMutationTestAccess::ApplySphereEditWithAllocationProbe(
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

	ValidationResult testing::VoxelMutationTestAccess::ApplySphereEditWithExhaustedRevision(
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
}
