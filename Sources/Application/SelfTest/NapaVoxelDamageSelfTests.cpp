#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/Edit/VoxelDamage.h"
#include "NapaVoxelCore/Edit/VoxelMutation.h"
#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Hash/VoxelWorldHash.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"
#include "NapaVoxelCore/Meshing/ReferenceMesher.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] napa::voxel::VoxelWorldConfig MakeDamageConfig(
			napa::voxel::CellAabb bounds = {
				.m_Min = { -8, -8, -8 },
				.m_MaxExclusive = { 8, 8, 8 },
			}) noexcept
		{
			return {
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = bounds,
			};
		}

		[[nodiscard]] napa::voxel::SphereEditRequest MakeStoneEdit(
			napa::voxel::Double3 center = {}, double radius = 1.0,
			double strength = 1.0, std::uint8_t damagePerHit = 128,
			std::uint8_t breakThreshold = 255) noexcept
		{
			return {
				.m_Brush = {
					.m_CenterWorld = center,
					.m_Radius = radius,
					.m_Strength = strength,
				},
				.m_MaterialRules = {
					.m_DamagePerHit = damagePerHit,
					.m_StoneBreakThreshold = breakThreshold,
				},
			};
		}

		[[nodiscard]] bool CreateStoneWorld(
			std::unique_ptr<napa::voxel::VoxelWorld>& world,
			const napa::voxel::VoxelWorldConfig& config = MakeDamageConfig(),
			double radius = 4.0)
		{
			using namespace napa::voxel;
			const PrimitiveDesc primitive{
				.m_StableId = { 1 },
				.m_Priority = { 0 },
				.m_Material = VoxelMaterial::Stone,
				.m_Shape = PrimitiveShape::Sphere,
				.m_Parameters = {
					.m_Sphere = {
						.m_Center = {},
						.m_Radius = radius,
					},
				},
			};
			PrimitiveWorldGenerationResult generation{};
			return GeneratePrimitiveVoxelWorld(
				config, std::span{ &primitive, 1 }, world, generation).Succeeded();
		}

		[[nodiscard]] bool HashDamageWorld(
			const napa::voxel::VoxelWorld& world, std::uint64_t& hash) noexcept
		{
			return napa::voxel::ComputeLogicalVoxelWorldHash(world, hash).Succeeded();
		}

		[[nodiscard]] bool CompareDamageWorlds(
			const napa::voxel::VoxelWorld& lhs,
			const napa::voxel::VoxelWorld& rhs) noexcept
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

		[[nodiscard]] bool IsCanonicalMarkerOrder(
			std::span<const napa::voxel::VoxelDamageMarker> markers) noexcept
		{
			for (std::size_t index = 1; index < markers.size(); ++index)
			{
				const napa::voxel::SampleCoord previous = markers[index - 1].m_Sample;
				const napa::voxel::SampleCoord current = markers[index].m_Sample;
				if (std::tie(current.m_Z, current.m_Y, current.m_X) <=
					std::tie(previous.m_Z, previous.m_Y, previous.m_X))
				{
					return false;
				}
			}
			return true;
		}

		void RunStoneTransitionTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			SphereEditContext centerContext{};
			SphereEditContext retainedContext{};
			SphereEditContext surfaceContext{};
			const bool contextsPrepared = PrepareSphereEditContext(
				MakeDamageConfig(), MakeStoneEdit({}, 1.0), centerContext).Succeeded() &&
				PrepareSphereEditContext(MakeDamageConfig(),
					MakeStoneEdit({}, 1.0, 0.5, 100, 250), retainedContext).Succeeded() &&
				PrepareSphereEditContext(MakeDamageConfig(),
					MakeStoneEdit({}, 1.0, 1.0, 100, 250), surfaceContext).Succeeded();
			const VoxelSample stone{
				.m_Density = 200,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 0,
			};
			VoxelSample firstHit{};
			VoxelSample secondHit{};
			const bool twoHitsEvaluated = contextsPrepared &&
				EvaluateSphereEditSampleTransition(
					centerContext, {}, stone, firstHit).Succeeded() &&
				EvaluateSphereEditSampleTransition(
					centerContext, {}, firstHit, secondHit).Succeeded();
			context.Check(twoHitsEvaluated && firstHit == VoxelSample{
				.m_Density = 200,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 128,
				} && secondHit == VoxelSample{
				.m_Density = 64,
				.m_Material = VoxelMaterial::Empty,
				.m_Damage = 0,
				}, "Stone damage is deterministic and the threshold hit reuses Sphere Subtract");

			const VoxelSample overshootStone{
				.m_Density = 255,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 200,
			};
			VoxelSample overshoot{};
			VoxelSample repeated{};
			const bool overshootEvaluated = contextsPrepared &&
				EvaluateSphereEditSampleTransition(surfaceContext,
					{ 1, 0, 0 }, overshootStone, overshoot).Succeeded() &&
				EvaluateSphereEditSampleTransition(surfaceContext,
					{ 1, 0, 0 }, overshoot, repeated).Succeeded();
			context.Check(overshootEvaluated && overshoot == VoxelSample{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 255,
				} && repeated == overshoot,
				"Threshold overshoot saturates and remains on a surviving Stone Sample");

			VoxelSample retained{};
			VoxelSample continued{};
			context.Check(contextsPrepared && EvaluateSphereEditSampleTransition(
				retainedContext, {}, overshootStone, retained).Succeeded() &&
				retained == VoxelSample{
					.m_Density = 160,
					.m_Material = VoxelMaterial::Stone,
					.m_Damage = 255,
				} && EvaluateSphereEditSampleTransition(
					retainedContext, {}, retained, continued).Succeeded() &&
				continued == VoxelSample{
					.m_Density = 112,
					.m_Material = VoxelMaterial::Empty,
					.m_Damage = 0,
				}, "A threshold Stone Sample retains Damage and continues subtracting until Empty");

			SphereEditContext disabledContext{};
			VoxelSample disabledOutput{};
			const VoxelSample thresholdStone{
				.m_Density = 200,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 255,
			};
			context.Check(PrepareSphereEditContext(MakeDamageConfig(),
				MakeStoneEdit({}, 1.0, 1.0, 0, 255), disabledContext).Succeeded() &&
				EvaluateSphereEditSampleTransition(
					disabledContext, {}, thresholdStone, disabledOutput).Succeeded() &&
				disabledOutput == thresholdStone,
				"A disabled Stone Damage path cannot subtract a previously damaged Sample");

			SphereEditContext zeroStrengthContext{};
			VoxelSample zeroStrengthOutput{};
			context.Check(PrepareSphereEditContext(MakeDamageConfig(),
				MakeStoneEdit({}, 1.0, 0.0), zeroStrengthContext).Succeeded() &&
				!zeroStrengthContext.HasDensityPath() && zeroStrengthContext.HasDamagePath() &&
				EvaluateSphereEditSampleTransition(
					zeroStrengthContext, {}, stone, zeroStrengthOutput).Succeeded() &&
				zeroStrengthOutput == VoxelSample{
					.m_Density = 200,
					.m_Material = VoxelMaterial::Stone,
					.m_Damage = 128,
				}, "Zero Strength keeps the Stone Damage path active without subtracting");

			const VoxelSample soil{
				.m_Density = 200,
				.m_Material = VoxelMaterial::Soil,
				.m_Damage = 0,
			};
			VoxelSample soilOutput{};
			context.Check(contextsPrepared && EvaluateSphereEditSampleTransition(
				centerContext, {}, soil, soilOutput).Succeeded() && soilOutput == VoxelSample{
					.m_Density = 64,
					.m_Material = VoxelMaterial::Empty,
					.m_Damage = 0,
				}, "Stone material rules do not alter the Soil subtract path");
		}

		void RunStoneMutationGoldenTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			std::unique_ptr<VoxelWorld> world;
			if (!CreateStoneWorld(world))
			{
				context.Check(false, "Stone mutation fixture creates its primitive world");
				return;
			}

			std::uint64_t initialHash = 0;
			std::uint64_t firstHash = 0;
			std::uint64_t secondHash = 0;
			VoxelMutationResult first{};
			VoxelMutationResult second{};
			const SphereEditRequest request = MakeStoneEdit({ 3.0, 0.0, 0.0 }, 0.5);
			const bool mutated = HashDamageWorld(*world, initialHash) &&
				ApplySphereEdit(*world, request, first).Succeeded() &&
				HashDamageWorld(*world, firstHash) &&
				ApplySphereEdit(*world, request, second).Succeeded() &&
				HashDamageWorld(*world, secondHash);
			context.Check(mutated && first.GetChangeKind() == VoxelMutationChangeKind::DamageOnly &&
				first.m_BaseWorldVoxelRevision == 1 && first.m_TargetWorldVoxelRevision == 2 &&
				first.m_SampleChanges.size() == 1 && first.m_DataDirtyChunks.size() == 1 &&
				first.m_MeshDirtyChunks.empty() &&
				initialHash == 16580640415625430688ull &&
				firstHash == 3945431654558813216ull,
				"The first Stone hit matches its exact Damage-only Golden");
			context.Check(mutated && second.GetChangeKind() ==
				VoxelMutationChangeKind::SurfaceChanged &&
				second.m_BaseWorldVoxelRevision == 2 && second.m_TargetWorldVoxelRevision == 3 &&
				second.m_SampleChanges.size() == 1 && second.m_DataDirtyChunks.size() == 1 &&
				second.m_MeshDirtyChunks.size() == 4 &&
				secondHash == 6835358999813787010ull,
				"The second Stone hit matches its exact Surface Dirty Golden");

			CpuMeshBatch damageOnlyBatch{};
			context.Check(BuildCpuMeshBatch(*world, first, damageOnlyBatch).m_Error ==
				ValidationError::MismatchedCpuMeshTargetRevision,
				"A superseded Damage-only Mutation cannot create a Mesh Batch");

			std::unique_ptr<VoxelWorld> damageOnlyWorld;
			VoxelMutationResult damageOnlyMutation{};
			CpuMeshBatch rejectedDamageOnlyBatch{};
			context.Check(CreateStoneWorld(damageOnlyWorld) &&
				ApplySphereEdit(*damageOnlyWorld, request, damageOnlyMutation).Succeeded() &&
				damageOnlyMutation.GetChangeKind() == VoxelMutationChangeKind::DamageOnly &&
				BuildCpuMeshBatch(*damageOnlyWorld, damageOnlyMutation,
					rejectedDamageOnlyBatch).m_Error == ValidationError::InvalidCpuMeshCandidateSet,
				"A current Damage-only Mutation cannot create an empty Mesh Batch");

			std::unique_ptr<VoxelWorld> negativeWorld;
			VoxelMutationResult negativeFirst{};
			VoxelMutationResult negativeSecond{};
			const SphereEditRequest negativeRequest = MakeStoneEdit({ -3.0, 0.0, 0.0 }, 0.5);
			context.Check(CreateStoneWorld(negativeWorld) &&
				ApplySphereEdit(*negativeWorld, negativeRequest, negativeFirst).Succeeded() &&
				ApplySphereEdit(*negativeWorld, negativeRequest, negativeSecond).Succeeded() &&
				negativeFirst.GetChangeKind() == VoxelMutationChangeKind::DamageOnly &&
				negativeSecond.GetChangeKind() == VoxelMutationChangeKind::SurfaceChanged &&
				negativeSecond.m_SampleChanges.size() == 1 &&
				negativeSecond.m_SampleChanges[0].m_Coordinate == SampleCoord{ -3, 0, 0 } &&
				negativeSecond.m_MeshDirtyChunks.size() == 4,
				"Negative-coordinate Stone hits preserve the same two-hit contract");

			std::unique_ptr<VoxelWorld> boundaryWorld;
			VoxelMutationResult boundaryFirst{};
			VoxelMutationResult boundarySecond{};
			const SphereEditRequest boundaryRequest = MakeStoneEdit({}, 0.5);
			context.Check(CreateStoneWorld(boundaryWorld) &&
				ApplySphereEdit(*boundaryWorld, boundaryRequest, boundaryFirst).Succeeded() &&
				ApplySphereEdit(*boundaryWorld, boundaryRequest, boundarySecond).Succeeded() &&
				boundaryFirst.GetChangeKind() == VoxelMutationChangeKind::DamageOnly &&
				boundarySecond.GetChangeKind() == VoxelMutationChangeKind::SurfaceChanged &&
				boundarySecond.m_MeshDirtyChunks.size() == 8,
				"A Chunk-corner Stone Sample dirties all eight Mesh owners after threshold");

			struct BoundaryCase
			{
				Double3 m_Center{};
				std::size_t m_ExpectedMeshDirtyCount = 0;
			};
			const std::array boundaryCases{
				BoundaryCase{ { 0.0, 2.0, 2.0 }, 2 },
				BoundaryCase{ { 0.0, 0.0, 2.0 }, 4 },
			};
			bool allBoundaryCasesMatched = true;
			for (const BoundaryCase& boundaryCase : boundaryCases)
			{
				std::unique_ptr<VoxelWorld> caseWorld;
				VoxelMutationResult caseFirst{};
				VoxelMutationResult caseSecond{};
				const SphereEditRequest caseRequest = MakeStoneEdit(boundaryCase.m_Center, 0.5);
				allBoundaryCasesMatched = allBoundaryCasesMatched &&
					CreateStoneWorld(caseWorld) &&
					ApplySphereEdit(*caseWorld, caseRequest, caseFirst).Succeeded() &&
					ApplySphereEdit(*caseWorld, caseRequest, caseSecond).Succeeded() &&
					caseFirst.GetChangeKind() == VoxelMutationChangeKind::DamageOnly &&
					caseSecond.GetChangeKind() == VoxelMutationChangeKind::SurfaceChanged &&
					caseSecond.m_MeshDirtyChunks.size() ==
					boundaryCase.m_ExpectedMeshDirtyCount;
			}
			context.Check(allBoundaryCasesMatched,
				"Chunk-face and Chunk-edge Stone hits derive their exact Mesh owners");

			std::unique_ptr<VoxelWorld> zeroStrengthWorld;
			ReferenceWorldMeshingResult initialMesh{};
			ReferenceWorldMeshingResult damageOnlyMesh{};
			VoxelMutationResult zeroStrengthFirst{};
			VoxelMutationResult zeroStrengthSecond{};
			VoxelMutationResult zeroStrengthThird{};
			VoxelSample zeroStrengthInitialSample{};
			VoxelSample zeroStrengthSample{};
			const SphereEditRequest zeroStrengthRequest =
				MakeStoneEdit({ 3.0, 0.0, 0.0 }, 0.5, 0.0);
			const bool zeroStrengthMutated = CreateStoneWorld(zeroStrengthWorld) &&
				zeroStrengthWorld->ReadCurrentSample(
					{ 3, 0, 0 }, zeroStrengthInitialSample).Succeeded() &&
				ReferenceMesher(*zeroStrengthWorld).MeshWorld(initialMesh).Succeeded() &&
				ApplySphereEdit(*zeroStrengthWorld,
					zeroStrengthRequest, zeroStrengthFirst).Succeeded() &&
				ReferenceMesher(*zeroStrengthWorld).MeshWorld(damageOnlyMesh).Succeeded() &&
				ApplySphereEdit(*zeroStrengthWorld,
					zeroStrengthRequest, zeroStrengthSecond).Succeeded() &&
				ApplySphereEdit(*zeroStrengthWorld,
					zeroStrengthRequest, zeroStrengthThird).Succeeded() &&
				zeroStrengthWorld->ReadCurrentSample(
					{ 3, 0, 0 }, zeroStrengthSample).Succeeded();
			context.Check(zeroStrengthMutated &&
				zeroStrengthFirst.GetChangeKind() == VoxelMutationChangeKind::DamageOnly &&
				zeroStrengthFirst.m_BaseWorldVoxelRevision == 1 &&
				zeroStrengthFirst.m_TargetWorldVoxelRevision == 2 &&
				zeroStrengthFirst.m_SampleChanges.size() == 1 &&
				zeroStrengthFirst.m_DataDirtyChunks.size() == 1 &&
				zeroStrengthFirst.m_MeshDirtyChunks.empty() &&
				zeroStrengthSecond.GetChangeKind() == VoxelMutationChangeKind::DamageOnly &&
				zeroStrengthSecond.m_BaseWorldVoxelRevision == 2 &&
				zeroStrengthSecond.m_TargetWorldVoxelRevision == 3 &&
				zeroStrengthSecond.m_SampleChanges.size() == 1 &&
				zeroStrengthSecond.m_DataDirtyChunks.size() == 1 &&
				zeroStrengthSecond.m_MeshDirtyChunks.empty() &&
				zeroStrengthThird.GetChangeKind() == VoxelMutationChangeKind::None &&
				zeroStrengthThird.m_BaseWorldVoxelRevision == 3 &&
				zeroStrengthThird.m_TargetWorldVoxelRevision == 3 &&
				zeroStrengthThird.m_SampleChanges.empty() &&
				zeroStrengthThird.m_DataDirtyChunks.empty() &&
				zeroStrengthThird.m_MeshDirtyChunks.empty() &&
				zeroStrengthWorld->GetWorldVoxelRevision() == 3 &&
				zeroStrengthSample.m_Density == zeroStrengthInitialSample.m_Density &&
				zeroStrengthSample.m_Material == zeroStrengthInitialSample.m_Material &&
				zeroStrengthSample.m_Damage == 255,
				"Zero Strength applies Damage-only hits until saturation without changing Density");
			context.Check(zeroStrengthMutated &&
				initialMesh.m_Validation == damageOnlyMesh.m_Validation &&
				initialMesh.m_BoundaryValidation == damageOnlyMesh.m_BoundaryValidation,
				"A Damage-only Stone hit preserves world mesh hash, counts, bounds, and contours");

			std::unique_ptr<VoxelWorld> disjointWorld;
			VoxelMutationResult positiveHit{};
			VoxelMutationResult disjointHit{};
			VoxelSample positiveSample{};
			context.Check(CreateStoneWorld(disjointWorld) &&
				ApplySphereEdit(*disjointWorld, request, positiveHit).Succeeded() &&
				ApplySphereEdit(*disjointWorld, negativeRequest, disjointHit).Succeeded() &&
				positiveHit.GetChangeKind() == VoxelMutationChangeKind::DamageOnly &&
				disjointHit.GetChangeKind() == VoxelMutationChangeKind::DamageOnly &&
				disjointWorld->ReadCurrentSample({ 3, 0, 0 }, positiveSample).Succeeded() &&
				positiveSample.m_Material == VoxelMaterial::Stone &&
				positiveSample.m_Damage == 128,
				"A disjoint second Stone hit cannot subtract the previously damaged region");
		}

		void RunStoneFullDomainOracleTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			struct OracleCase
			{
				SphereEditRequest m_Request{};
				std::size_t m_HitCount = 0;
			};
			const std::array oracleCases{
				OracleCase{ MakeStoneEdit({ -0.25, 0.5, -0.75 }, 1.75), 2 },
				OracleCase{ MakeStoneEdit({ 3.0, 0.0, 0.0 }, 0.5, 0.0), 3 },
			};
			bool allCasesMatched = true;
			for (const OracleCase& oracleCase : oracleCases)
			{
				std::unique_ptr<VoxelWorld> boundedWorld;
				std::unique_ptr<VoxelWorld> oracleWorld;
				allCasesMatched = allCasesMatched &&
					CreateStoneWorld(boundedWorld) && CreateStoneWorld(oracleWorld);
				for (std::size_t hit = 0;
					hit < oracleCase.m_HitCount && allCasesMatched; ++hit)
				{
					VoxelMutationResult boundedMutation{};
					SphereEditContext editContext{};
					allCasesMatched = ApplySphereEdit(*boundedWorld,
						oracleCase.m_Request, boundedMutation).Succeeded() &&
						PrepareSphereEditContext(oracleWorld->GetConfig(),
							oracleCase.m_Request, editContext).Succeeded();
					std::vector<VoxelSampleChange> oracleChanges;
					const SampleAabb bounds = oracleWorld->GetLogicalSampleBounds();
					for (std::int32_t z = bounds.m_Min.m_Z;
						z < bounds.m_MaxExclusive.m_Z && allCasesMatched; ++z)
					{
						for (std::int32_t y = bounds.m_Min.m_Y;
							y < bounds.m_MaxExclusive.m_Y && allCasesMatched; ++y)
						{
							for (std::int32_t x = bounds.m_Min.m_X;
								x < bounds.m_MaxExclusive.m_X; ++x)
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

					std::vector<ChunkCoord> oracleDataDirty;
					std::vector<ChunkCoord> oracleMeshDirty;
					std::uint64_t boundedHash = 0;
					std::uint64_t oracleHash = 0;
					allCasesMatched = allCasesMatched &&
						DeriveVoxelMutationDirtyChunks(oracleWorld->GetConfig(), oracleChanges,
							oracleDataDirty, oracleMeshDirty).Succeeded() &&
						boundedMutation.m_SampleChanges == oracleChanges &&
						boundedMutation.m_DataDirtyChunks == oracleDataDirty &&
						boundedMutation.m_MeshDirtyChunks == oracleMeshDirty &&
						CompareDamageWorlds(*boundedWorld, *oracleWorld) &&
						HashDamageWorld(*boundedWorld, boundedHash) &&
						HashDamageWorld(*oracleWorld, oracleHash) && boundedHash == oracleHash;
				}
			}
			context.Check(allCasesMatched,
				"Stone density and zero-Strength Damage hits match the full-domain Oracle exactly");
		}

		void RunDamageMarkerSnapshotTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			const VoxelWorldConfig largeConfig = MakeDamageConfig({
				.m_Min = { -16, -16, -16 },
				.m_MaxExclusive = { 16, 16, 16 },
				});
			std::unique_ptr<VoxelWorld> world;
			VoxelMutationResult damageMutation{};
			VoxelDamageMarkerSnapshot snapshot{};
			const bool built = CreateStoneWorld(world, largeConfig, 10.0) &&
				ApplySphereEdit(*world, MakeStoneEdit({}, 10.0), damageMutation).Succeeded() &&
				damageMutation.GetChangeKind() == VoxelMutationChangeKind::DamageOnly &&
				BuildVoxelDamageMarkerSnapshot(
					*world, damageMutation.m_TargetWorldVoxelRevision, snapshot).Succeeded();
			context.Check(built && snapshot.m_SourceWorldVoxelRevision ==
				damageMutation.m_TargetWorldVoxelRevision &&
				snapshot.m_Markers.size() == DefaultDamageMarkerLimit &&
				snapshot.m_TotalDamagedSampleCount == 4169 &&
				snapshot.m_Truncated && IsCanonicalMarkerOrder(snapshot.m_Markers) &&
				!snapshot.m_Markers.empty() &&
				snapshot.m_Markers.front().m_Sample == SampleCoord{ 0, 0, -10 } &&
				snapshot.m_Markers.front().m_WorldPosition == Double3{
					static_cast<double>(snapshot.m_Markers.front().m_Sample.m_X),
					static_cast<double>(snapshot.m_Markers.front().m_Sample.m_Y),
					static_cast<double>(snapshot.m_Markers.front().m_Sample.m_Z),
				}, "Damage Marker snapshots are revision-tagged, canonical, and bounded");

			const VoxelDamageMarkerSnapshot sentinel{
				.m_SourceWorldVoxelRevision = 91,
				.m_TotalDamagedSampleCount = 92,
				.m_Truncated = true,
				.m_Markers = {
					{
						.m_Sample = { 1, 2, 3 },
						.m_WorldPosition = { 4.0, 5.0, 6.0 },
						.m_Damage = 7,
					},
				},
			};
			VoxelDamageMarkerSnapshot unchanged = sentinel;
			context.Check(BuildVoxelDamageMarkerSnapshot(
				*world, damageMutation.m_BaseWorldVoxelRevision, unchanged).m_Error ==
				ValidationError::MismatchedDamageMarkerSourceRevision && unchanged == sentinel,
				"A stale Damage Marker request leaves its destination unchanged");
		}
	}

	void RunNapaVoxelDamageSelfTests(SelfTestContext& context) noexcept
	{
		RunStoneTransitionTests(context);
		RunStoneMutationGoldenTests(context);
		RunStoneFullDomainOracleTests(context);
		RunDamageMarkerSnapshotTests(context);
	}
}
