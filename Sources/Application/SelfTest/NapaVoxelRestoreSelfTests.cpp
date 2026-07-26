#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/Hash/VoxelWorldHash.h"
#include "NapaVoxelCore/World/VoxelRestore.h"
#include "NapaVoxelCore/World/VoxelSample.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] napa::voxel::VoxelWorldConfig
			MakeRestoreConfig() noexcept
		{
			return {
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 0.25f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = { -8, -8, -8 },
					.m_MaxExclusive = { 16, 16, 16 },
				},
			};
		}

		[[nodiscard]] bool InitializeSample(
			napa::voxel::VoxelWorld& world,
			napa::voxel::SampleCoord coordinate,
			napa::voxel::VoxelSample sample) noexcept
		{
			bool changed = false;
			return world.WriteOriginalAndCurrentSample(
				coordinate,
				sample,
				changed).Succeeded() && changed;
		}

		[[nodiscard]] bool MutateSample(
			napa::voxel::VoxelWorld& world,
			napa::voxel::SampleCoord coordinate,
			napa::voxel::VoxelSample sample) noexcept
		{
			bool changed = false;
			return world.WriteCurrentSample(
				coordinate,
				sample,
				changed).Succeeded() && changed;
		}

		void RunRestoreTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const VoxelSample stone{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 3,
			};
			const VoxelSample soil{
				.m_Density = IsoValue + 1,
				.m_Material = VoxelMaterial::Soil,
				.m_Damage = 7,
			};
			const VoxelSample damagedStone{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 91,
			};
			const VoxelSample damagedSoil{
				.m_Density = IsoValue + 1,
				.m_Material = VoxelMaterial::Soil,
				.m_Damage = 83,
			};

			const SampleCoord regionCoordinate{ -8, -8, -8 };
			const SampleCoord firstChunkCoordinate{ 0, 0, 0 };
			const SampleCoord secondChunkCoordinate{ 1, 0, 0 };
			const SampleCoord guardCoordinate{ 16, 0, 0 };

			std::unique_ptr<VoxelWorld> world;
			context.Check(
				VoxelWorld::Create(MakeRestoreConfig(), world).Succeeded() &&
					world &&
					InitializeSample(*world, regionCoordinate, stone) &&
					InitializeSample(*world, firstChunkCoordinate, stone) &&
					InitializeSample(*world, secondChunkCoordinate, soil) &&
					InitializeSample(*world, guardCoordinate, soil),
				"Restore test data initializes original and current samples");
			if (!world)
			{
				return;
			}

			std::uint64_t initialHash = 0;
			context.Check(
				ComputeLogicalVoxelWorldHash(
					*world,
					initialHash).Succeeded(),
				"Restore tests capture the initial logical world hash");

			context.Check(
				MutateSample(
					*world,
					regionCoordinate,
					damagedStone) &&
					MutateSample(
						*world,
						firstChunkCoordinate,
						damagedStone) &&
					MutateSample(
						*world,
						secondChunkCoordinate,
						damagedSoil) &&
					MutateSample(
						*world,
						guardCoordinate,
						damagedSoil),
				"Restore test data diverges current samples from original");

			RestoreResult result{};
			const std::uint64_t regionRevision =
				world->GetWorldVoxelRevision();
			context.Check(
				world->RestoreRegion(
					{
						.m_Min = regionCoordinate,
						.m_MaxExclusive = { -7, -7, -7 },
					},
					result).Succeeded() &&
					result.m_ChangedSampleCoordinates ==
						std::vector<SampleCoord>{ regionCoordinate } &&
					world->GetWorldVoxelRevision() ==
						regionRevision + 1,
				"Region restore reports exact changes and advances world revision once");

			VoxelSample original{};
			VoxelSample current{};
			context.Check(
				world->ReadOriginalSample(
					regionCoordinate,
					original).Succeeded() &&
					world->ReadCurrentSample(
						regionCoordinate,
						current).Succeeded() &&
					original == stone &&
					current == stone,
				"Region restore copies original bytes without changing original data");

			const std::uint64_t noOpRevision =
				world->GetWorldVoxelRevision();
			context.Check(
				world->RestoreRegion(
					{
						.m_Min = regionCoordinate,
						.m_MaxExclusive = { -7, -7, -7 },
					},
					result).Succeeded() &&
					!result.Changed() &&
					world->GetWorldVoxelRevision() == noOpRevision,
				"A no-op region restore clears its result without advancing revision");

			const VoxelChunk* const sharedChunk =
				world->FindChunk({ 0, 0, 0 });
			const std::uint64_t sharedChunkRevision =
				sharedChunk ? sharedChunk->GetVoxelRevision() : 0;
			const std::uint64_t chunkRestoreRevision =
				world->GetWorldVoxelRevision();
			context.Check(
				sharedChunk &&
					world->RestoreSampleOwnerChunk(
						{ 0, 0, 0 },
						result).Succeeded() &&
					result.m_ChangedSampleCoordinates ==
						std::vector<SampleCoord>{
							firstChunkCoordinate,
							secondChunkCoordinate,
						} &&
					world->GetWorldVoxelRevision() ==
						chunkRestoreRevision + 1 &&
					sharedChunk->GetVoxelRevision() ==
						sharedChunkRevision + 1,
				"Chunk restore batches changes in canonical order and advances revisions once");

			const std::size_t residentBeforeNoOp =
				world->GetResidentChunkCount();
			const std::uint64_t revisionBeforeNoOp =
				world->GetWorldVoxelRevision();
			context.Check(
				world->RestoreSampleOwnerChunk(
					{ 0, 1, 0 },
					result).Succeeded() &&
					!result.Changed() &&
					world->GetResidentChunkCount() ==
						residentBeforeNoOp &&
					world->GetWorldVoxelRevision() ==
						revisionBeforeNoOp,
				"Restoring an unallocated logical owner does not allocate storage");

			result.m_ChangedSampleCoordinates = {
				firstChunkCoordinate,
			};
			const std::uint64_t revisionBeforeRejectedRestore =
				world->GetWorldVoxelRevision();
			context.Check(
				world->RestoreSampleOwnerChunk(
					{ 3, 0, 0 },
					result).m_Error ==
					ValidationError::ChunkOutsideLogicalSampleDomain &&
					result.m_ChangedSampleCoordinates ==
						std::vector<SampleCoord>{
							firstChunkCoordinate,
						} &&
					world->GetWorldVoxelRevision() ==
						revisionBeforeRejectedRestore,
				"A rejected chunk restore leaves its result and world unchanged");

			const std::uint64_t guardRestoreRevision =
				world->GetWorldVoxelRevision();
			context.Check(
				world->RestoreSampleOwnerChunk(
					{ 2, 0, 0 },
					result).Succeeded() &&
					result.m_ChangedSampleCoordinates ==
						std::vector<SampleCoord>{ guardCoordinate } &&
					world->GetWorldVoxelRevision() ==
						guardRestoreRevision + 1,
				"Sample-owner guard chunk restore includes only logical samples");

			context.Check(
				MutateSample(
					*world,
					regionCoordinate,
					damagedStone) &&
					MutateSample(
						*world,
						guardCoordinate,
						damagedSoil),
				"Restore-all test data diverges samples in separate chunks");

			const std::uint64_t restoreAllRevision =
				world->GetWorldVoxelRevision();
			std::uint64_t restoredHash = 0;
			context.Check(
				world->RestoreAll(result).Succeeded() &&
					result.m_ChangedSampleCoordinates ==
						std::vector<SampleCoord>{
							regionCoordinate,
							guardCoordinate,
						} &&
					world->GetWorldVoxelRevision() ==
						restoreAllRevision + 1 &&
					ComputeLogicalVoxelWorldHash(
						*world,
						restoredHash).Succeeded() &&
					restoredHash == initialHash,
				"Restore all returns canonical changes and recovers the initial hash");

			result.m_ChangedSampleCoordinates = {
				guardCoordinate,
			};
			context.Check(
				world->RestoreRegion(
					{
						.m_Min = { -9, 0, 0 },
						.m_MaxExclusive = { -8, 1, 1 },
					},
					result).m_Error ==
					ValidationError::SampleOutsideLogicalBounds &&
					result.m_ChangedSampleCoordinates ==
						std::vector<SampleCoord>{ guardCoordinate },
				"An out-of-bounds restore region leaves its result unchanged");

			context.Check(
				world->RestoreRegion(
					{
						.m_Min = { 0, 0, 0 },
						.m_MaxExclusive = { 0, 1, 1 },
					},
					result).m_Error ==
					ValidationError::EmptySampleBounds &&
					result.m_ChangedSampleCoordinates ==
						std::vector<SampleCoord>{ guardCoordinate },
				"An empty restore region is rejected without changing its result");
		}
	}

	void RunNapaVoxelRestoreSelfTests(SelfTestContext& context) noexcept
	{
		RunRestoreTests(context);
	}
}
