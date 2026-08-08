#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/Edit/VoxelMutation.h"
#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Hash/VoxelWorldHash.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] napa::voxel::VoxelWorldConfig MakeMutationConfig(
			float surfaceBandVoxels = 2.0f) noexcept
		{
			return {
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = surfaceBandVoxels,
				.m_LogicalCellBounds = {
					.m_Min = { -8, -8, -8 },
					.m_MaxExclusive = { 8, 8, 8 },
				},
			};
		}

		[[nodiscard]] napa::voxel::SphereEditRequest MakeSoilEdit(
			napa::voxel::Double3 center = {}, double radius = 1.0,
			double strength = 1.0) noexcept
		{
			return {
				.m_Brush = {
					.m_CenterWorld = center,
					.m_Radius = radius,
					.m_Strength = strength,
				},
				.m_MaterialRules = {
					.m_DamagePerHit = 0,
					.m_StoneBreakThreshold = 255,
				},
			};
		}

		[[nodiscard]] napa::voxel::VoxelSampleChange MakeDensityChange(
			napa::voxel::SampleCoord coordinate) noexcept
		{
			using namespace napa::voxel;
			return {
				.m_Coordinate = coordinate,
				.m_Before = {
					.m_Density = 200,
					.m_Material = VoxelMaterial::Soil,
					.m_Damage = 0,
				},
				.m_After = {
					.m_Density = 100,
					.m_Material = VoxelMaterial::Empty,
					.m_Damage = 0,
				},
			};
		}

		[[nodiscard]] napa::voxel::VoxelWorldConfig MakeDirtyConfig() noexcept
		{
			napa::voxel::VoxelWorldConfig config = MakeMutationConfig();
			config.m_LogicalCellBounds = {
				.m_Min = { -16, -16, -16 },
				.m_MaxExclusive = { 16, 16, 16 },
			};
			return config;
		}

		[[nodiscard]] bool CreateSoilWorld(
			std::unique_ptr<napa::voxel::VoxelWorld>& world)
		{
			using namespace napa::voxel;
			const PrimitiveDesc primitive{
				.m_StableId = { 1 },
				.m_Priority = { 0 },
				.m_Material = VoxelMaterial::Soil,
				.m_Shape = PrimitiveShape::Sphere,
				.m_Parameters = {
					.m_Sphere = {
						.m_Center = {},
						.m_Radius = 4.0,
					},
				},
			};
			PrimitiveWorldGenerationResult generation{};
			return GeneratePrimitiveVoxelWorld(
				MakeMutationConfig(), std::span{ &primitive, 1 }, world, generation).Succeeded();
		}

		[[nodiscard]] bool HashWorld(
			const napa::voxel::VoxelWorld& world, std::uint64_t& hash) noexcept
		{
			return napa::voxel::ComputeLogicalVoxelWorldHash(world, hash).Succeeded();
		}

		[[nodiscard]] bool CompareWorldSamples(
			const napa::voxel::VoxelWorld& lhs, const napa::voxel::VoxelWorld& rhs) noexcept
		{
			using namespace napa::voxel;
			const SampleAabb bounds = lhs.GetLogicalSampleBounds();
			if (bounds != rhs.GetLogicalSampleBounds())
			{
				return false;
			}
			for (std::int32_t z = bounds.m_Min.m_Z; z < bounds.m_MaxExclusive.m_Z; ++z)
			{
				for (std::int32_t y = bounds.m_Min.m_Y; y < bounds.m_MaxExclusive.m_Y; ++y)
				{
					for (std::int32_t x = bounds.m_Min.m_X; x < bounds.m_MaxExclusive.m_X; ++x)
					{
						VoxelSample lhsSample{};
						VoxelSample rhsSample{};
						const SampleCoord coordinate{ x, y, z };
						if (lhs.ReadCurrentSample(coordinate, lhsSample).Failed() ||
							rhs.ReadCurrentSample(coordinate, rhsSample).Failed() ||
							lhsSample != rhsSample)
						{
							return false;
						}
					}
				}
			}
			return true;
		}

		void RunSoilTransitionTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			context.Check(std::is_standard_layout_v<VoxelSampleChange> &&
				std::is_trivially_copyable_v<VoxelSampleChange>,
				"Voxel Sample changes use a portable value layout");
			const VoxelSample solid{
				.m_Density = 200,
				.m_Material = VoxelMaterial::Soil,
				.m_Damage = 0,
			};
			SphereEditContext midpointContext{};
			SphereEditContext fullContext{};
			const bool contextsPrepared = PrepareSphereEditContext(
				MakeMutationConfig(), MakeSoilEdit({}, 1.0, 0.5), midpointContext).Succeeded() &&
				PrepareSphereEditContext(
					MakeMutationConfig(), MakeSoilEdit({}, 1.0, 1.0), fullContext).Succeeded();
			VoxelSample midpoint{};
			VoxelSample full{};
			const bool transitionsEvaluated = contextsPrepared &&
				EvaluateSphereEditSampleTransition(midpointContext, {}, solid, midpoint).Succeeded() &&
				EvaluateSphereEditSampleTransition(fullContext, {}, solid, full).Succeeded();
			context.Check(transitionsEvaluated && midpoint == VoxelSample{
				.m_Density = 132,
				.m_Material = VoxelMaterial::Soil,
				.m_Damage = 0,
				} && full == VoxelSample{
					.m_Density = 64,
					.m_Material = VoxelMaterial::Empty,
					.m_Damage = 0,
				}, "Soil subtract honors midpoint strength and canonicalizes a full cut");

			SphereEditContext tieContext{};
			const bool tiePrepared = PrepareSphereEditContext(MakeMutationConfig(127.0f),
				MakeSoilEdit({}, 1.0, 0.5), tieContext).Succeeded();
			VoxelSample positiveTie{};
			VoxelSample negativeTie{};
			const bool tiesEvaluated = tiePrepared &&
				EvaluateSphereEditSampleTransition(tieContext, { 1, 0, 0 }, {
					.m_Density = 129,
					.m_Material = VoxelMaterial::Soil,
					.m_Damage = 0,
					}, positiveTie).Succeeded() &&
					EvaluateSphereEditSampleTransition(tieContext, {}, {
						.m_Density = 128,
						.m_Material = VoxelMaterial::Soil,
						.m_Damage = 0,
						}, negativeTie).Succeeded();
					context.Check(tiesEvaluated && positiveTie.m_Density == 129 &&
						positiveTie.m_Material == VoxelMaterial::Soil &&
						negativeTie.m_Density == 127 &&
						negativeTie.m_Material == VoxelMaterial::Empty,
						"Soil subtract rounds positive and negative half ties away from zero");

					VoxelSample preserved{
						.m_Density = 77,
						.m_Material = VoxelMaterial::Empty,
						.m_Damage = 0,
					};
					const VoxelSample expectedPreserved = preserved;
					const ValidationResult invalidResult = EvaluateSphereEditSampleTransition(
						fullContext, {}, {
							.m_Density = 255,
							.m_Material = static_cast<VoxelMaterial>(255),
							.m_Damage = 0,
						}, preserved);
						context.Check(invalidResult.m_Error == ValidationError::InvalidVoxelMaterial &&
							preserved == expectedPreserved,
							"Soil transition rejects an unknown Material without changing output");
		}

		void RunAtomicSoilMutationTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			std::unique_ptr<VoxelWorld> world;
			if (!CreateSoilWorld(world))
			{
				context.Check(false, "Atomic Soil fixture creates its primitive world");
				return;
			}

			const std::uint64_t baseRevision = world->GetWorldVoxelRevision();
			VoxelMutationResult mutation{};
			const ValidationResult mutationResult = ApplySphereEdit(
				*world, MakeSoilEdit({ -0.25, 0.5, -0.75 }, 1.75, 1.0), mutation);
			std::uint64_t editedHash = 0;
			const bool hashesComputed = HashWorld(*world, editedHash);

			bool changesValid = mutationResult.Succeeded() && mutation.Changed() &&
				mutation.m_BaseWorldVoxelRevision == baseRevision &&
				mutation.m_TargetWorldVoxelRevision == baseRevision + 1 &&
				world->GetWorldVoxelRevision() == mutation.m_TargetWorldVoxelRevision;
			SampleCoordZYXLess sampleLess{};
			changesValid = changesValid && std::ranges::is_sorted(
				mutation.m_SampleChanges,
				[&sampleLess](const VoxelSampleChange& lhs, const VoxelSampleChange& rhs) noexcept
				{ return sampleLess(lhs.m_Coordinate, rhs.m_Coordinate); });
			std::map<ChunkCoord, bool, ChunkCoordZYXLess> changedOwners;
			for (const VoxelSampleChange& change : mutation.m_SampleChanges)
			{
				VoxelSample current{};
				VoxelSample original{};
				OwnedSampleAddress address{};
				changesValid = changesValid && change.m_Before != change.m_After &&
					ValidateVoxelSample(change.m_Before).Succeeded() &&
					ValidateVoxelSample(change.m_After).Succeeded() &&
					world->ReadCurrentSample(change.m_Coordinate, current).Succeeded() &&
					world->ReadOriginalSample(change.m_Coordinate, original).Succeeded() &&
					ResolveSampleOwner(change.m_Coordinate,
						world->GetConfig().m_ChunkCellCount, address).Succeeded() &&
					current == change.m_After && original == change.m_Before;
				changedOwners[address.m_Owner] = true;
			}
			for (const auto& [owner, changed] : changedOwners)
			{
				const VoxelChunk* chunk = world->FindChunk(owner);
				changesValid = changesValid && changed && chunk != nullptr &&
					chunk->GetVoxelRevision() == mutation.m_TargetWorldVoxelRevision;
			}
			std::vector<ChunkCoord> derivedDataDirty;
			std::vector<ChunkCoord> derivedMeshDirty;
			const ValidationResult dirtyResult = DeriveVoxelMutationDirtyChunks(
				world->GetConfig(), mutation.m_SampleChanges,
				derivedDataDirty, derivedMeshDirty);
			context.Check(changesValid && hashesComputed &&
				dirtyResult.Succeeded() &&
				mutation.m_DataDirtyChunks == derivedDataDirty &&
				mutation.m_MeshDirtyChunks == derivedMeshDirty &&
				!mutation.m_DataDirtyChunks.empty() &&
				!mutation.m_MeshDirtyChunks.empty() &&
				mutation.m_SampleChanges.size() == 97 &&
				editedHash == 397877192563327680ull,
				"One Soil Brush commits canonical changes and one shared World revision");

			const std::uint64_t settledRevision = world->GetWorldVoxelRevision();
			VoxelMutationResult repeated{
				.m_BaseWorldVoxelRevision = 99,
				.m_TargetWorldVoxelRevision = 100,
				.m_SampleChanges = { mutation.m_SampleChanges.front() },
				.m_DataDirtyChunks = { { 2, 3, 4 } },
				.m_MeshDirtyChunks = { { -2, -3, -4 } },
			};
			const ValidationResult repeatedResult = ApplySphereEdit(
				*world, MakeSoilEdit({ -0.25, 0.5, -0.75 }, 1.75, 1.0), repeated);
			context.Check(repeatedResult.Succeeded() && !repeated.Changed() &&
				repeated.m_DataDirtyChunks.empty() && repeated.m_MeshDirtyChunks.empty() &&
				repeated.m_BaseWorldVoxelRevision == settledRevision &&
				repeated.m_TargetWorldVoxelRevision == settledRevision &&
				world->GetWorldVoxelRevision() == settledRevision,
				"A repeated settled Soil Brush is a successful no-op with no Revision advance");
		}

		void RunCurrentFirstAndChunkRevisionTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			std::unique_ptr<VoxelWorld> world;
			if (VoxelWorld::Create(MakeMutationConfig(), world).Failed())
			{
				context.Check(false, "Current-first mutation fixture creates an empty world");
				return;
			}

			bool changed = false;
			const VoxelSample solid{
				.m_Density = 255,
				.m_Material = VoxelMaterial::Soil,
				.m_Damage = 0,
			};
			const bool seeded = world->WriteCurrentSample({}, solid, changed).Succeeded() && changed;
			VoxelMutationResult mutation{};
			const bool edited = seeded &&
				ApplySphereEdit(*world, MakeSoilEdit({}, 1.0, 1.0), mutation).Succeeded();
			VoxelSample original{};
			VoxelSample current{};
			const VoxelChunk* chunk = world->FindChunk({});
			context.Check(edited && mutation.Changed() &&
				world->ReadOriginalSample({}, original).Succeeded() &&
				world->ReadCurrentSample({}, current).Succeeded() &&
				original == DefaultVoxelSample && current.m_Density == 64 &&
				current.m_Material == VoxelMaterial::Empty && chunk != nullptr &&
				chunk->GetVoxelRevision() == mutation.m_TargetWorldVoxelRevision &&
				world->GetResidentChunkCount() == 1,
				"Sphere mutation preserves Original in a Current-first resident Chunk");

			std::unique_ptr<VoxelWorld> revisionWorld;
			const bool revisionWorldCreated =
				VoxelWorld::Create(MakeMutationConfig(), revisionWorld).Succeeded();
			bool changedA = false;
			bool changedB = false;
			bool changedAAgain = false;
			const bool writesSucceeded = revisionWorldCreated &&
				revisionWorld->WriteCurrentSample({ 0, 0, 0 }, solid, changedA).Succeeded() &&
				revisionWorld->WriteCurrentSample({ -1, 0, 0 }, solid, changedB).Succeeded() &&
				revisionWorld->WriteCurrentSample({ 0, 0, 0 }, {
					.m_Density = 200,
					.m_Material = VoxelMaterial::Soil,
					.m_Damage = 0,
					}, changedAAgain).Succeeded();
				const VoxelChunk* chunkA = revisionWorldCreated ? revisionWorld->FindChunk({}) : nullptr;
				const VoxelChunk* chunkB =
					revisionWorldCreated ? revisionWorld->FindChunk({ -1, 0, 0 }) : nullptr;
				context.Check(writesSucceeded && changedA && changedB && changedAAgain &&
					revisionWorld->GetWorldVoxelRevision() == 3 && chunkA != nullptr &&
					chunkB != nullptr && chunkA->GetVoxelRevision() == 3 &&
					chunkB->GetVoxelRevision() == 2,
					"Chunk revisions record the last World revision that changed their Samples");
		}

		void RunExactDirtyChunkTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			const VoxelWorldConfig config = MakeDirtyConfig();
			const ChunkCoord origin{};

			const VoxelSampleChange interiorChange = MakeDensityChange({ 1, 1, 1 });
			std::vector<ChunkCoord> dataDirty;
			std::vector<ChunkCoord> meshDirty;
			const ValidationResult interiorResult = DeriveVoxelMutationDirtyChunks(
				config, std::span{ &interiorChange, 1 }, dataDirty, meshDirty);
			context.Check(interiorResult.Succeeded() &&
				dataDirty == std::vector<ChunkCoord>{ origin } &&
				meshDirty == std::vector<ChunkCoord>{ origin },
				"An interior Sample change dirties one Data owner and one Mesh owner");

			const VoxelSampleChange faceChange = MakeDensityChange({ 8, 1, 1 });
			const ValidationResult faceResult = DeriveVoxelMutationDirtyChunks(
				config, std::span{ &faceChange, 1 }, dataDirty, meshDirty);
			context.Check(faceResult.Succeeded() &&
				dataDirty == std::vector<ChunkCoord>{ { 1, 0, 0 } } &&
				meshDirty == std::vector<ChunkCoord>{ origin, { 1, 0, 0 } },
				"A Chunk-face Sample change dirties both adjacent Mesh owners");

			const VoxelSampleChange edgeChange = MakeDensityChange({ 8, 8, 1 });
			const ValidationResult edgeResult = DeriveVoxelMutationDirtyChunks(
				config, std::span{ &edgeChange, 1 }, dataDirty, meshDirty);
			context.Check(edgeResult.Succeeded() &&
				dataDirty == std::vector<ChunkCoord>{ { 1, 1, 0 } } &&
				meshDirty == std::vector<ChunkCoord>{
					origin, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 },
				}, "A Chunk-edge Sample change dirties all four adjacent Mesh owners");

			const VoxelSampleChange cornerChange = MakeDensityChange({ 8, 8, 8 });
			const std::vector<ChunkCoord> expectedCornerMesh{
				origin, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 },
				{ 0, 0, 1 }, { 1, 0, 1 }, { 0, 1, 1 }, { 1, 1, 1 },
			};
			const ValidationResult cornerResult = DeriveVoxelMutationDirtyChunks(
				config, std::span{ &cornerChange, 1 }, dataDirty, meshDirty);
			context.Check(cornerResult.Succeeded() &&
				dataDirty == std::vector<ChunkCoord>{ { 1, 1, 1 } } &&
				meshDirty == expectedCornerMesh,
				"A Chunk-corner Sample change dirties all eight adjacent Mesh owners");

			const VoxelSampleChange negativeCornerChange = MakeDensityChange({ 0, 0, 0 });
			const std::vector<ChunkCoord> expectedNegativeCornerMesh{
				{ -1, -1, -1 }, { 0, -1, -1 }, { -1, 0, -1 }, { 0, 0, -1 },
				{ -1, -1, 0 }, { 0, -1, 0 }, { -1, 0, 0 }, origin,
			};
			const ValidationResult negativeResult = DeriveVoxelMutationDirtyChunks(
				config, std::span{ &negativeCornerChange, 1 }, dataDirty, meshDirty);
			context.Check(negativeResult.Succeeded() &&
				dataDirty == std::vector<ChunkCoord>{ origin } &&
				meshDirty == expectedNegativeCornerMesh,
				"Negative Chunk owners use the same exact corner-dirty contract");

			const VoxelSampleChange damageOnlyChange{
				.m_Coordinate = { -1, -1, -1 },
				.m_Before = {
					.m_Density = 200,
					.m_Material = VoxelMaterial::Stone,
					.m_Damage = 10,
				},
				.m_After = {
					.m_Density = 200,
					.m_Material = VoxelMaterial::Stone,
					.m_Damage = 11,
				},
			};
			const ValidationResult damageResult = DeriveVoxelMutationDirtyChunks(
				config, std::span{ &damageOnlyChange, 1 }, dataDirty, meshDirty);
			context.Check(damageResult.Succeeded() &&
				dataDirty == std::vector<ChunkCoord>{ { -1, -1, -1 } } && meshDirty.empty(),
				"A Damage-only Sample change dirties Data without invalidating Mesh");

			VoxelWorldConfig clippedConfig = MakeMutationConfig();
			clippedConfig.m_LogicalCellBounds = {
				.m_Min = {},
				.m_MaxExclusive = { 8, 8, 8 },
			};
			const std::array clippedChanges{
				MakeDensityChange({ 0, 0, 0 }),
				MakeDensityChange({ 8, 8, 8 }),
			};
			const ValidationResult clippedResult = DeriveVoxelMutationDirtyChunks(
				clippedConfig, clippedChanges, dataDirty, meshDirty);
			context.Check(clippedResult.Succeeded() &&
				dataDirty == std::vector<ChunkCoord>{ origin, { 1, 1, 1 } } &&
				meshDirty == std::vector<ChunkCoord>{ origin },
				"Mesh Dirty cells are clipped exactly to Logical Cell Bounds");

			const std::array repeatedChanges{
				cornerChange,
				MakeDensityChange({ 7, 7, 7 }),
			};
			const ValidationResult repeatedResult = DeriveVoxelMutationDirtyChunks(
				config, repeatedChanges, dataDirty, meshDirty);
			const std::array reversedChanges{
				repeatedChanges[1],
				repeatedChanges[0],
			};
			std::vector<ChunkCoord> reversedDataDirty;
			std::vector<ChunkCoord> reversedMeshDirty;
			const ValidationResult reversedResult = DeriveVoxelMutationDirtyChunks(
				config, reversedChanges, reversedDataDirty, reversedMeshDirty);
			context.Check(repeatedResult.Succeeded() && reversedResult.Succeeded() &&
				dataDirty == std::vector<ChunkCoord>{ origin, { 1, 1, 1 } } &&
				meshDirty == expectedCornerMesh && dataDirty == reversedDataDirty &&
				meshDirty == reversedMeshDirty,
				"Dirty Chunk sets are sorted, unique, and independent of change order");

			const VoxelSampleChange invalidChange{
				.m_Coordinate = {},
				.m_Before = DefaultVoxelSample,
				.m_After = DefaultVoxelSample,
			};
			const std::vector<ChunkCoord> sentinelData{ { 7, 8, 9 } };
			const std::vector<ChunkCoord> sentinelMesh{ { -7, -8, -9 } };
			dataDirty = sentinelData;
			meshDirty = sentinelMesh;
			const ValidationResult invalidResult = DeriveVoxelMutationDirtyChunks(
				config, std::span{ &invalidChange, 1 }, dataDirty, meshDirty);
			context.Check(invalidResult.m_Error == ValidationError::InvalidVoxelSampleChange &&
				dataDirty == sentinelData && meshDirty == sentinelMesh,
				"Invalid Sample changes leave Dirty outputs unchanged");
		}

		void RunMutationFailureAtomicityTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			std::unique_ptr<VoxelWorld> world;
			if (!CreateSoilWorld(world))
			{
				context.Check(false, "Mutation failure fixture creates its primitive world");
				return;
			}

			std::uint64_t beforeHash = 0;
			const bool beforeHashComputed = HashWorld(*world, beforeHash);
			const std::uint64_t beforeRevision = world->GetWorldVoxelRevision();
			const std::size_t beforeResidents = world->GetResidentChunkCount();
			VoxelMutationResult output{
				.m_BaseWorldVoxelRevision = 41,
				.m_TargetWorldVoxelRevision = 42,
				.m_SampleChanges = { {
					.m_Coordinate = { 1, 2, 3 },
					.m_Before = DefaultVoxelSample,
					.m_After = DefaultVoxelSample,
				} },
				.m_DataDirtyChunks = { { 4, 5, 6 } },
				.m_MeshDirtyChunks = { { -4, -5, -6 } },
			};
			const VoxelMutationResult expectedOutput = output;
			SphereEditRequest invalid = MakeSoilEdit();
			invalid.m_Brush.m_Strength = std::numeric_limits<double>::quiet_NaN();
			const ValidationResult invalidResult = ApplySphereEdit(*world, invalid, output);

			SphereEditRequest overflow = MakeSoilEdit();
			overflow.m_Brush.m_Radius = std::numeric_limits<double>::max();
			const ValidationResult overflowResult = ApplySphereEdit(*world, overflow, output);
			std::uint64_t afterHash = 0;
			const bool afterHashComputed = HashWorld(*world, afterHash);
			context.Check(beforeHashComputed && afterHashComputed &&
				invalidResult.m_Error == ValidationError::NonFiniteEditStrength &&
				overflowResult.m_Error == ValidationError::ArithmeticOverflow &&
				output == expectedOutput && beforeHash == afterHash &&
				world->GetWorldVoxelRevision() == beforeRevision &&
				world->GetResidentChunkCount() == beforeResidents,
				"Mutation Prepare failures preserve Samples, Residency, Revision, and output");
		}

		void RunRevisionAndAllocationFailureTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			const SphereEditRequest request =
				MakeSoilEdit({ -0.25, 0.5, -0.75 }, 1.75, 1.0);

			std::unique_ptr<VoxelWorld> revisionWorld;
			if (!CreateSoilWorld(revisionWorld))
			{
				context.Check(false, "Revision exhaustion fixture creates its primitive world");
				return;
			}
			VoxelMutationTestAccess::SetWorldVoxelRevision(
				*revisionWorld, std::numeric_limits<std::uint64_t>::max());
			std::uint64_t revisionHashBefore = 0;
			std::uint64_t revisionHashAfter = 0;
			const std::size_t revisionResidents = revisionWorld->GetResidentChunkCount();
			VoxelMutationResult revisionOutput{
				.m_BaseWorldVoxelRevision = 17,
				.m_TargetWorldVoxelRevision = 18,
				.m_DataDirtyChunks = { { 2, 3, 4 } },
				.m_MeshDirtyChunks = { { -2, -3, -4 } },
			};
			const VoxelMutationResult expectedRevisionOutput = revisionOutput;
			const bool revisionHashBeforeComputed =
				HashWorld(*revisionWorld, revisionHashBefore);
			const ValidationResult revisionResult =
				ApplySphereEdit(*revisionWorld, request, revisionOutput);
			const bool revisionHashAfterComputed = HashWorld(*revisionWorld, revisionHashAfter);
			context.Check(revisionHashBeforeComputed && revisionHashAfterComputed &&
				revisionResult.m_Error == ValidationError::ArithmeticOverflow &&
				revisionWorld->GetWorldVoxelRevision() ==
				std::numeric_limits<std::uint64_t>::max() &&
				revisionWorld->GetResidentChunkCount() == revisionResidents &&
				revisionHashBefore == revisionHashAfter &&
				revisionOutput == expectedRevisionOutput,
				"Revision exhaustion rejects a changing mutation atomically");

			std::unique_ptr<VoxelWorld> successfulWorld;
			if (!CreateSoilWorld(successfulWorld))
			{
				context.Check(false, "Allocation probe fixture creates its primitive world");
				return;
			}
			VoxelMutationResult successfulOutput{};
			VoxelMutationAllocationProbe successfulProbe{};
			const ValidationResult successfulResult =
				VoxelMutationTestAccess::ApplySphereEditWithAllocationProbe(
					*successfulWorld, request, successfulOutput, successfulProbe);
			const std::size_t prepareAllocationCount =
				successfulProbe.m_PrepareAllocationCount;
			context.Check(successfulResult.Succeeded() && successfulOutput.Changed() &&
				prepareAllocationCount > 0 && successfulProbe.m_CommitAllocationCount == 0,
				"A successful mutation observes Prepare allocations and no Commit allocations");

			bool everyFailureWasAtomic = successfulResult.Succeeded() &&
				prepareAllocationCount > 0 && successfulProbe.m_CommitAllocationCount == 0;
			for (std::size_t allocation = 1;
				everyFailureWasAtomic && allocation <= prepareAllocationCount; ++allocation)
			{
				std::unique_ptr<VoxelWorld> faultWorld;
				if (!CreateSoilWorld(faultWorld))
				{
					everyFailureWasAtomic = false;
					break;
				}

				std::uint64_t hashBefore = 0;
				std::uint64_t hashAfter = 0;
				const std::uint64_t revisionBefore = faultWorld->GetWorldVoxelRevision();
				const std::size_t residentsBefore = faultWorld->GetResidentChunkCount();
				std::vector<std::uint64_t> chunkRevisionsBefore;
				bool chunkRevisionsPreserved = true;
				for (const ChunkCoord owner : successfulOutput.m_DataDirtyChunks)
				{
					const VoxelChunk* chunk = faultWorld->FindChunk(owner);
					if (chunk == nullptr)
					{
						chunkRevisionsPreserved = false;
						break;
					}
					chunkRevisionsBefore.push_back(chunk->GetVoxelRevision());
				}
				VoxelMutationResult faultOutput{
					.m_BaseWorldVoxelRevision = 21,
					.m_TargetWorldVoxelRevision = 22,
					.m_DataDirtyChunks = { { 5, 6, 7 } },
					.m_MeshDirtyChunks = { { -5, -6, -7 } },
				};
				const VoxelMutationResult expectedFaultOutput = faultOutput;
				VoxelMutationAllocationProbe faultProbe{
					.m_FailAtPrepareAllocation = allocation,
				};
				const bool hashBeforeComputed = HashWorld(*faultWorld, hashBefore);
				const ValidationResult faultResult =
					VoxelMutationTestAccess::ApplySphereEditWithAllocationProbe(
						*faultWorld, request, faultOutput, faultProbe);
				const bool hashAfterComputed = HashWorld(*faultWorld, hashAfter);
				for (std::size_t index = 0;
					chunkRevisionsPreserved && index < successfulOutput.m_DataDirtyChunks.size();
					++index)
				{
					const VoxelChunk* chunk =
						faultWorld->FindChunk(successfulOutput.m_DataDirtyChunks[index]);
					chunkRevisionsPreserved = chunk != nullptr &&
						chunk->GetVoxelRevision() == chunkRevisionsBefore[index];
				}
				everyFailureWasAtomic = hashBeforeComputed && hashAfterComputed &&
					faultResult.m_Error == ValidationError::VoxelMutationAllocationFailure &&
					faultProbe.m_PrepareAllocationCount == allocation &&
					faultProbe.m_CommitAllocationCount == 0 &&
					faultWorld->GetWorldVoxelRevision() == revisionBefore &&
					faultWorld->GetResidentChunkCount() == residentsBefore &&
					chunkRevisionsPreserved && hashBefore == hashAfter &&
					faultOutput == expectedFaultOutput;
			}
			context.Check(everyFailureWasAtomic,
				"Every observed Prepare allocation fault preserves World and output atomically");
		}

		void RunFullDomainMutationOracleTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			const std::array requests{
				MakeSoilEdit({ -0.25, 0.5, -0.75 }, 1.75, 0.0),
				MakeSoilEdit({ -0.25, 0.5, -0.75 }, 1.75, 0.5),
				MakeSoilEdit({ -0.25, 0.5, -0.75 }, 1.75, 1.0),
			};

			bool allCasesMatched = true;
			for (const SphereEditRequest& request : requests)
			{
				std::unique_ptr<VoxelWorld> boundedWorld;
				std::unique_ptr<VoxelWorld> oracleWorld;
				if (!CreateSoilWorld(boundedWorld) || !CreateSoilWorld(oracleWorld))
				{
					allCasesMatched = false;
					break;
				}

				VoxelMutationResult boundedMutation{};
				SphereEditContext editContext{};
				if (ApplySphereEdit(*boundedWorld, request, boundedMutation).Failed() ||
					PrepareSphereEditContext(
						oracleWorld->GetConfig(), request, editContext).Failed())
				{
					allCasesMatched = false;
					break;
				}

				std::vector<VoxelSampleChange> oracleChanges;
				const SampleAabb bounds = oracleWorld->GetLogicalSampleBounds();
				for (std::int32_t z = bounds.m_Min.m_Z; z < bounds.m_MaxExclusive.m_Z; ++z)
				{
					for (std::int32_t y = bounds.m_Min.m_Y; y < bounds.m_MaxExclusive.m_Y; ++y)
					{
						for (std::int32_t x = bounds.m_Min.m_X; x < bounds.m_MaxExclusive.m_X; ++x)
						{
							const SampleCoord coordinate{ x, y, z };
							VoxelSample before{};
							VoxelSample after{};
							bool changed = false;
							if (oracleWorld->ReadCurrentSample(coordinate, before).Failed() ||
								EvaluateSphereEditSampleTransition(
									editContext, coordinate, before, after).Failed())
							{
								allCasesMatched = false;
								break;
							}
							if (before != after)
							{
								oracleChanges.push_back({ coordinate, before, after });
								if (oracleWorld->WriteCurrentSample(
									coordinate, after, changed).Failed() || !changed)
								{
									allCasesMatched = false;
									break;
								}
							}
						}
					}
				}

				std::uint64_t boundedHash = 0;
				std::uint64_t oracleHash = 0;
				std::vector<ChunkCoord> oracleDataDirty;
				std::vector<ChunkCoord> oracleMeshDirty;
				const ValidationResult oracleDirtyResult = DeriveVoxelMutationDirtyChunks(
					oracleWorld->GetConfig(), oracleChanges,
					oracleDataDirty, oracleMeshDirty);
				allCasesMatched = allCasesMatched &&
					oracleDirtyResult.Succeeded() &&
					boundedMutation.m_SampleChanges == oracleChanges &&
					boundedMutation.m_DataDirtyChunks == oracleDataDirty &&
					boundedMutation.m_MeshDirtyChunks == oracleMeshDirty &&
					CompareWorldSamples(*boundedWorld, *oracleWorld) &&
					HashWorld(*boundedWorld, boundedHash) &&
					HashWorld(*oracleWorld, oracleHash) && boundedHash == oracleHash;
				if (!allCasesMatched)
				{
					break;
				}
			}
			context.Check(allCasesMatched,
				"Bounded Soil mutation matches a full-domain transition Oracle exactly");
		}
	}

	void RunNapaVoxelMutationSelfTests(SelfTestContext& context) noexcept
	{
		RunSoilTransitionTests(context);
		RunAtomicSoilMutationTests(context);
		RunCurrentFirstAndChunkRevisionTests(context);
		RunExactDirtyChunkTests(context);
		RunMutationFailureAtomicityTests(context);
		RunRevisionAndAllocationFailureTests(context);
		RunFullDomainMutationOracleTests(context);
	}
}
