#include "NapaVoxelCore/Edit/VoxelMutation.h"

#include "NapaVoxelCore/Validation/CheckedArithmetic.h"
#include "NapaVoxelCore/World/VoxelChunk.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace napa::voxel
{
	namespace
	{
		struct PreparedSampleWrite
		{
			VoxelChunk* m_Chunk = nullptr;
			std::size_t m_FlatIndex = 0;
			VoxelSample m_After{};
		};

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

			VoxelSample prepared = before;
			if (before.m_Material == VoxelMaterial::Soil && evaluation.m_DensityPathEligible)
			{
				const std::int32_t currentSigned =
					static_cast<std::int32_t>(before.m_Density) -
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
				prepared.m_Density = static_cast<std::uint8_t>(resultSigned + IsoValue);
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
		static_assert(std::allocator_traits<
			std::vector<VoxelSampleChange>::allocator_type>::is_always_equal::value);
		static_assert(std::is_nothrow_invocable_r_v<bool, ChunkCoordZYXLess,
			ChunkCoord, ChunkCoord>);

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
				result.m_BaseWorldVoxelRevision = preparedResult.m_BaseWorldVoxelRevision;
				result.m_TargetWorldVoxelRevision = preparedResult.m_TargetWorldVoxelRevision;
				result.m_SampleChanges.swap(preparedResult.m_SampleChanges);
			};
		static_assert(noexcept(commitMutation()));

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
					if (!evaluation.m_DensityPathEligible)
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
							std::unique_ptr<VoxelChunk> created;
							const ValidationResult createResult = VoxelChunk::Create(
								world.m_Config.m_ChunkCellCount, created);
							if (createResult.Failed())
							{
								return createResult;
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

					preparedResult.m_SampleChanges.push_back({
						.m_Coordinate = coordinate,
						.m_Before = before,
						.m_After = after,
						});
					preparedWrites.push_back({
						.m_Chunk = chunk,
						.m_FlatIndex = flatIndex,
						.m_After = after,
						});
				}
			}
		}

		if (!preparedResult.Changed())
		{
			commitMutation();
			return {};
		}

		const auto targetRevision =
			CheckedAdd(world.m_WorldVoxelRevision, std::uint64_t{ 1 });
		if (!targetRevision.has_value())
		{
			return { ValidationError::ArithmeticOverflow };
		}
		preparedResult.m_TargetWorldVoxelRevision = *targetRevision;

		commitMutation();
		return {};
	}
}
