#include "NapaVoxelTestFramework.h"

#include "NapaVoxelCore/Hash/VoxelWorldHash.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"
#include "NapaVoxelCore/World/VoxelRestore.h"
#include "NapaVoxelCore/World/VoxelSample.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace napa::voxel::testing
{
	namespace
	{
		[[nodiscard]] napa::voxel::VoxelWorldConfig MakeRestoreConfig() noexcept
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

		[[nodiscard]] bool InitializeSample(napa::voxel::VoxelWorld& world,
			napa::voxel::SampleCoord coordinate, napa::voxel::VoxelSample sample) noexcept
		{
			bool changed = false;
			return world.WriteOriginalAndCurrentSample(
				coordinate, sample, changed).Succeeded() && changed;
		}

		[[nodiscard]] bool MutateSample(napa::voxel::VoxelWorld& world,
			napa::voxel::SampleCoord coordinate, napa::voxel::VoxelSample sample) noexcept
		{
			bool changed = false;
			return world.WriteCurrentSample(coordinate, sample, changed).Succeeded() && changed;
		}

		[[nodiscard]] napa::voxel::VoxelMutationResult MakeRestoreSentinel()
		{
			using namespace napa::voxel;
			return {
				.m_BaseWorldVoxelRevision = 91,
				.m_TargetWorldVoxelRevision = 92,
				.m_SampleChanges = {
					{
						.m_Coordinate = { 1, 2, 3 },
						.m_Before = DefaultVoxelSample,
						.m_After = {
							.m_Density = IsoValue,
							.m_Material = VoxelMaterial::Stone,
						},
					},
				},
				.m_DataDirtyChunks = { { 4, 5, 6 } },
				.m_MeshDirtyChunks = { { 7, 8, 9 } },
			};
		}

		[[nodiscard]] bool CreateDivergedRestoreWorld(
			std::unique_ptr<napa::voxel::VoxelWorld>& world)
		{
			using namespace napa::voxel;
			const VoxelSample stone{
				.m_Density = 200,
				.m_Material = VoxelMaterial::Stone,
			};
			const VoxelSample damagedStone{
				.m_Density = 200,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 100,
			};
			const VoxelSample soil{
				.m_Density = 220,
				.m_Material = VoxelMaterial::Soil,
			};
			return VoxelWorld::Create(MakeRestoreConfig(), world).Succeeded() && world &&
				InitializeSample(*world, { 0, 0, 0 }, stone) &&
				InitializeSample(*world, { 1, 0, 0 }, soil) &&
				MutateSample(*world, { 0, 0, 0 }, damagedStone) &&
				MutateSample(*world, { 1, 0, 0 }, DefaultVoxelSample);
		}

		void RunExactRestoreTests(TestContext& context) noexcept
		{
			using namespace napa::voxel;
			const VoxelSample stone{
				.m_Density = 200,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 3,
			};
			const VoxelSample soil{
				.m_Density = 220,
				.m_Material = VoxelMaterial::Soil,
			};
			const VoxelSample damagedStone{
				.m_Density = 200,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 91,
			};
			const SampleCoord regionCoordinate{ -8, -8, -8 };
			const SampleCoord surfaceCoordinate{ 0, 0, 0 };
			const SampleCoord damageCoordinate{ 1, 0, 0 };
			const SampleCoord guardCoordinate{ 16, 0, 0 };

			std::unique_ptr<VoxelWorld> world;
			const bool initialized = VoxelWorld::Create(MakeRestoreConfig(), world).Succeeded() &&
				world && InitializeSample(*world, regionCoordinate, stone) &&
				InitializeSample(*world, surfaceCoordinate, soil) &&
				InitializeSample(*world, damageCoordinate, stone) &&
				InitializeSample(*world, guardCoordinate, soil);
			context.Check(initialized,
				"Restore test data initializes Original and Current Samples");
			if (!initialized)
			{
				return;
			}

			std::uint64_t initialHash = 0;
			const bool diverged = ComputeLogicalVoxelWorldHash(*world, initialHash).Succeeded() &&
				MutateSample(*world, regionCoordinate, damagedStone) &&
				MutateSample(*world, surfaceCoordinate, DefaultVoxelSample) &&
				MutateSample(*world, damageCoordinate, damagedStone) &&
				MutateSample(*world, guardCoordinate, DefaultVoxelSample);
			context.Check(diverged, "Restore test data diverges Damage and Surface Samples");
			if (!diverged)
			{
				return;
			}

			VoxelMutationResult result{};
			const std::uint64_t regionBaseRevision = world->GetWorldVoxelRevision();
			const bool regionRestored = RestoreRegion(*world, {
				.m_Min = regionCoordinate,
				.m_MaxExclusive = { -7, -7, -7 },
				}, result).Succeeded();
			context.Check(regionRestored &&
				result.m_BaseWorldVoxelRevision == regionBaseRevision &&
				result.m_TargetWorldVoxelRevision == regionBaseRevision + 1 &&
				result.m_SampleChanges == std::vector<VoxelSampleChange>{
					{ regionCoordinate, damagedStone, stone },
				} && result.GetChangeKind() == VoxelMutationChangeKind::DamageOnly &&
				result.m_DataDirtyChunks == std::vector<ChunkCoord>{ { -1, -1, -1 } } &&
				result.m_MeshDirtyChunks.empty() &&
				world->GetWorldVoxelRevision() == regionBaseRevision + 1,
				"Damage-only Region Restore emits the common exact Change and Dirty contract");

			CpuMeshBatch damageOnlyBatch{};
			context.Check(regionRestored &&
				BuildCpuMeshBatch(*world, result, damageOnlyBatch).m_Error ==
				ValidationError::InvalidCpuMeshCandidateSet,
				"Damage-only Restore cannot create an empty Mesh Batch");

			const std::uint64_t noOpRevision = world->GetWorldVoxelRevision();
			result = MakeRestoreSentinel();
			context.Check(RestoreRegion(*world, {
				.m_Min = regionCoordinate,
				.m_MaxExclusive = { -7, -7, -7 },
				}, result).Succeeded() && !result.Changed() &&
				result.m_BaseWorldVoxelRevision == noOpRevision &&
				result.m_TargetWorldVoxelRevision == noOpRevision &&
				result.m_DataDirtyChunks.empty() && result.m_MeshDirtyChunks.empty() &&
				world->GetWorldVoxelRevision() == noOpRevision,
				"A no-op Restore publishes an empty result without advancing Revision");

			const std::uint64_t chunkBaseRevision = world->GetWorldVoxelRevision();
			const VoxelChunk* const sharedChunk = world->FindChunk({ 0, 0, 0 });
			const bool chunkRestored = RestoreSampleOwnerChunk(*world, { 0, 0, 0 }, result).Succeeded();
			context.Check(chunkRestored && sharedChunk &&
				result.m_BaseWorldVoxelRevision == chunkBaseRevision &&
				result.m_TargetWorldVoxelRevision == chunkBaseRevision + 1 &&
				result.m_SampleChanges == std::vector<VoxelSampleChange>{
					{ surfaceCoordinate, DefaultVoxelSample, soil },
					{ damageCoordinate, damagedStone, stone },
				} && result.GetChangeKind() == VoxelMutationChangeKind::SurfaceChanged &&
				result.m_DataDirtyChunks == std::vector<ChunkCoord>{ { 0, 0, 0 } } &&
				result.m_MeshDirtyChunks.size() == 8 &&
				sharedChunk->GetVoxelRevision() == chunkBaseRevision + 1,
				"Chunk Restore batches canonical mixed changes under one World Revision");

			CpuMeshBatch restoreBatch{};
			context.Check(chunkRestored && BuildCpuMeshBatch(*world, result, restoreBatch).Succeeded() &&
				restoreBatch.m_RequestedChunks == result.m_MeshDirtyChunks &&
				restoreBatch.m_Candidates.size() == result.m_MeshDirtyChunks.size(),
				"Surface Restore builds exactly its Dirty Mesh replacement Batch");
			VoxelMutationResult missingDirty = result;
			missingDirty.m_MeshDirtyChunks.pop_back();
			VoxelMutationResult extraDirty = result;
			extraDirty.m_MeshDirtyChunks.push_back({ 1, 1, 1 });
			CpuMeshBatch unchangedBatch = restoreBatch;
			const ChunkMeshRecord* const unchangedCandidates = unchangedBatch.m_Candidates.data();
			context.Check(BuildCpuMeshBatch(*world, missingDirty, unchangedBatch).m_Error ==
				ValidationError::InvalidVoxelMutation &&
				BuildCpuMeshBatch(*world, extraDirty, unchangedBatch).m_Error ==
				ValidationError::InvalidVoxelMutation &&
				unchangedBatch.m_Candidates.data() == unchangedCandidates &&
				unchangedBatch.m_RequestedChunks == restoreBatch.m_RequestedChunks,
				"Missing and extra Restore Dirty Candidates fail without changing the Batch");

			const std::size_t residentBeforeNoOp = world->GetResidentChunkCount();
			const std::uint64_t revisionBeforeNoOp = world->GetWorldVoxelRevision();
			context.Check(RestoreSampleOwnerChunk(*world, { 0, 1, 0 }, result).Succeeded() &&
				!result.Changed() && world->GetResidentChunkCount() == residentBeforeNoOp &&
				world->GetWorldVoxelRevision() == revisionBeforeNoOp,
				"Restoring an unallocated logical owner does not allocate storage");

			const VoxelMutationResult sentinel = MakeRestoreSentinel();
			result = sentinel;
			context.Check(RestoreSampleOwnerChunk(*world, { 3, 0, 0 }, result).m_Error ==
				ValidationError::ChunkOutsideLogicalSampleDomain && result == sentinel &&
				world->GetWorldVoxelRevision() == revisionBeforeNoOp,
				"A rejected Chunk Restore preserves both World and output");

			const std::uint64_t guardBaseRevision = world->GetWorldVoxelRevision();
			context.Check(RestoreSampleOwnerChunk(*world, { 2, 0, 0 }, result).Succeeded() &&
				result.m_SampleChanges == std::vector<VoxelSampleChange>{
					{ guardCoordinate, DefaultVoxelSample, soil },
				} && world->GetWorldVoxelRevision() == guardBaseRevision + 1,
				"Sample-owner guard Chunk Restore includes only logical Samples");

			context.Check(MutateSample(*world, regionCoordinate, damagedStone) &&
				MutateSample(*world, guardCoordinate, DefaultVoxelSample),
				"Restore All fixture diverges Samples in separate Chunks");
			const std::uint64_t allBaseRevision = world->GetWorldVoxelRevision();
			std::uint64_t restoredHash = 0;
			context.Check(RestoreAll(*world, result).Succeeded() &&
				result.m_BaseWorldVoxelRevision == allBaseRevision &&
				result.m_TargetWorldVoxelRevision == allBaseRevision + 1 &&
				result.m_SampleChanges.size() == 2 &&
				result.GetChangeKind() == VoxelMutationChangeKind::SurfaceChanged &&
				result.m_DataDirtyChunks.size() == 2 && result.m_MeshDirtyChunks.size() == 4 &&
				ComputeLogicalVoxelWorldHash(*world, restoredHash).Succeeded() &&
				restoredHash == initialHash,
				"Restore All returns canonical changes and recovers the initial Voxel Hash");

			result = sentinel;
			context.Check(RestoreRegion(*world, {
				.m_Min = { -9, 0, 0 },
				.m_MaxExclusive = { -8, 1, 1 },
				}, result).m_Error == ValidationError::SampleOutsideLogicalBounds &&
				result == sentinel,
				"An out-of-bounds Restore Region preserves its output");
			context.Check(RestoreRegion(*world, {
				.m_Min = { 0, 0, 0 },
				.m_MaxExclusive = { 0, 1, 1 },
				}, result).m_Error == ValidationError::EmptySampleBounds && result == sentinel,
				"An empty Restore Region is rejected atomically");
		}

		void RunCurrentFirstRestoreTest(TestContext& context) noexcept
		{
			using namespace napa::voxel;
			std::unique_ptr<VoxelWorld> world;
			bool changed = false;
			const VoxelSample stone{
				.m_Density = 200,
				.m_Material = VoxelMaterial::Stone,
			};
			const SampleCoord coordinate{ -1, 0, 0 };
			const bool initialized = VoxelWorld::Create(MakeRestoreConfig(), world).Succeeded() &&
				world && world->WriteCurrentSample(coordinate, stone, changed).Succeeded() && changed;
			VoxelMutationResult result{};
			VoxelSample original{};
			VoxelSample current{};
			const std::uint64_t baseRevision = initialized ? world->GetWorldVoxelRevision() : 0;
			context.Check(initialized && RestoreAll(*world, result).Succeeded() &&
				result.m_SampleChanges == std::vector<VoxelSampleChange>{
					{ coordinate, stone, DefaultVoxelSample },
				} && result.m_BaseWorldVoxelRevision == baseRevision &&
				result.m_TargetWorldVoxelRevision == baseRevision + 1 &&
				world->ReadOriginalSample(coordinate, original).Succeeded() &&
				world->ReadCurrentSample(coordinate, current).Succeeded() &&
				original == DefaultVoxelSample && current == DefaultVoxelSample,
				"Current-first Restore recovers the implicit Original baseline exactly");
		}

		void RunMultiChunkRestoreOrderTest(TestContext& context) noexcept
		{
			using namespace napa::voxel;
			const VoxelSample stone{
				.m_Density = 200,
				.m_Material = VoxelMaterial::Stone,
			};
			const VoxelSample damagedStone{
				.m_Density = 200,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 77,
			};
			const std::array scrambledCoordinates{
				SampleCoord{ 9, 9, 9 },
				SampleCoord{ -7, 8, 1 },
				SampleCoord{ 8, -7, -7 },
				SampleCoord{ -1, -1, -1 },
				SampleCoord{ 15, 0, 8 },
				SampleCoord{ 0, 9, -7 },
			};

			std::unique_ptr<VoxelWorld> world;
			bool initialized = VoxelWorld::Create(MakeRestoreConfig(), world).Succeeded() && world;
			for (const SampleCoord coordinate : scrambledCoordinates)
			{
				initialized = initialized && InitializeSample(*world, coordinate, stone) &&
					MutateSample(*world, coordinate, damagedStone);
			}

			VoxelMutationResult result{};
			const std::vector<VoxelSampleChange> expectedChanges{
				{ { 8, -7, -7 }, damagedStone, stone },
				{ { 0, 9, -7 }, damagedStone, stone },
				{ { -1, -1, -1 }, damagedStone, stone },
				{ { -7, 8, 1 }, damagedStone, stone },
				{ { 15, 0, 8 }, damagedStone, stone },
				{ { 9, 9, 9 }, damagedStone, stone },
			};
			context.Check(initialized && RestoreAll(*world, result).Succeeded() &&
				result.m_SampleChanges == expectedChanges,
				"Restore All orders cross-Chunk Sample Changes by global z/y/x coordinates");
		}

		void RunSparseRestoreAllTest(TestContext& context) noexcept
		{
			using namespace napa::voxel;
			VoxelWorldConfig config = MakeRestoreConfig();
			config.m_LogicalCellBounds = {
				.m_Min = { -1'000'000'000, 0, 0 },
				.m_MaxExclusive = { 1'000'000'000, 1, 1 },
			};
			std::unique_ptr<VoxelWorld> world;
			bool changed = false;
			const VoxelSample stone{
				.m_Density = 200,
				.m_Material = VoxelMaterial::Stone,
			};
			VoxelMutationResult result{};
			context.Check(VoxelWorld::Create(config, world).Succeeded() && world &&
				world->WriteCurrentSample({ 0, 0, 0 }, stone, changed).Succeeded() && changed &&
				RestoreAll(*world, result).Succeeded() &&
				result.m_SampleChanges == std::vector<VoxelSampleChange>{
					{ { 0, 0, 0 }, stone, DefaultVoxelSample },
				} && world->GetResidentChunkCount() == 1,
				"Restore All scans sparse resident storage instead of the full logical domain");
		}

		void RunRestoreAtomicityTests(TestContext& context) noexcept
		{
			using namespace napa::voxel;
			using namespace napa::voxel::testing;
			std::unique_ptr<VoxelWorld> successfulWorld;
			VoxelMutationResult successfulResult{};
			VoxelRestoreAllocationProbe successfulProbe{};
			const bool restored = CreateDivergedRestoreWorld(successfulWorld) &&
				RestoreAllWithAllocationProbe(
					*successfulWorld, successfulResult, successfulProbe).Succeeded();
			const std::size_t allocationCount = successfulProbe.m_PrepareAllocationCount;
			context.Check(restored && successfulResult.Changed() && allocationCount > 0 &&
				successfulProbe.m_CommitAllocationCount == 0,
				"A successful Restore observes Prepare allocations and no Commit allocations");

			bool allAllocationFaultsAtomic = restored && allocationCount > 0;
			for (std::size_t failAt = 1; failAt <= allocationCount && allAllocationFaultsAtomic; ++failAt)
			{
				std::unique_ptr<VoxelWorld> world;
				std::uint64_t beforeHash = 0;
				std::uint64_t afterHash = 0;
				const VoxelMutationResult sentinel = MakeRestoreSentinel();
				VoxelMutationResult result = sentinel;
				VoxelRestoreAllocationProbe probe{ .m_FailAtPrepareAllocation = failAt };
				allAllocationFaultsAtomic = CreateDivergedRestoreWorld(world) &&
					ComputeLogicalVoxelWorldHash(*world, beforeHash).Succeeded();
				const std::uint64_t revision = allAllocationFaultsAtomic ?
					world->GetWorldVoxelRevision() : 0;
				const std::size_t residentCount = allAllocationFaultsAtomic ?
					world->GetResidentChunkCount() : 0;
				allAllocationFaultsAtomic = allAllocationFaultsAtomic &&
					RestoreAllWithAllocationProbe(
						*world, result, probe).m_Error ==
						ValidationError::VoxelMutationAllocationFailure &&
					probe.m_CommitAllocationCount == 0 && result == sentinel &&
					world->GetWorldVoxelRevision() == revision &&
					world->GetResidentChunkCount() == residentCount &&
					ComputeLogicalVoxelWorldHash(*world, afterHash).Succeeded() &&
					beforeHash == afterHash;
			}
			context.Check(allAllocationFaultsAtomic,
				"Every observed Restore Prepare allocation fault preserves World and output atomically");

			std::unique_ptr<VoxelWorld> exhaustedWorld;
			std::uint64_t beforeHash = 0;
			std::uint64_t afterHash = 0;
			const VoxelMutationResult sentinel = MakeRestoreSentinel();
			VoxelMutationResult exhaustedResult = sentinel;
			const bool exhaustedFixture = CreateDivergedRestoreWorld(exhaustedWorld) &&
				ComputeLogicalVoxelWorldHash(*exhaustedWorld, beforeHash).Succeeded();
			const std::uint64_t revision = exhaustedFixture ?
				exhaustedWorld->GetWorldVoxelRevision() : 0;
			context.Check(exhaustedFixture &&
				RestoreAllWithExhaustedRevision(
					*exhaustedWorld, exhaustedResult).m_Error ==
					ValidationError::ArithmeticOverflow && exhaustedResult == sentinel &&
				exhaustedWorld->GetWorldVoxelRevision() == revision &&
				ComputeLogicalVoxelWorldHash(*exhaustedWorld, afterHash).Succeeded() &&
				beforeHash == afterHash,
				"Restore Revision exhaustion preserves World and output atomically");
		}
	}

	void RunNapaVoxelRestoreSelfTests(TestContext& context) noexcept
	{
		RunExactRestoreTests(context);
		RunCurrentFirstRestoreTest(context);
		RunMultiChunkRestoreOrderTest(context);
		RunSparseRestoreAllTest(context);
		RunRestoreAtomicityTests(context);
	}
}
