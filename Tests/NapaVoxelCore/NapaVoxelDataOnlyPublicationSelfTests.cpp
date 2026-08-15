#include "NapaVoxelDataOnlyPublicationSelfTests.h"

#include "NapaVoxelCore/Edit/VoxelMutation.h"
#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"
#include "NapaVoxelCore/Meshing/DataOnlyPublication.h"
#include "NapaVoxelCore/Meshing/MeshValidation.h"
#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"
#include "NapaVoxelCore/World/VoxelSample.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

namespace napa::voxel::testing
{
	namespace
	{
		[[nodiscard]] napa::voxel::VoxelWorldConfig MakePublicationConfig() noexcept
		{
			return {
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = {},
					.m_MaxExclusive = { 8, 8, 8 },
					},
			};
		}

		[[nodiscard]] bool BuildCoreDataOnlyPublicationInput(
			std::unique_ptr<napa::voxel::VoxelWorld>& world,
			napa::voxel::VisibleMeshSet& visible,
			napa::voxel::VoxelMutationResult& mutation) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config = MakePublicationConfig();
			const PrimitiveDesc sphere{
				.m_StableId = { 1 },
				.m_Material = VoxelMaterial::Stone,
				.m_Shape = PrimitiveShape::Sphere,
				.m_Parameters = {
					.m_Sphere = {
						.m_Center = { 4.0, 4.0, 4.0 },
						.m_Radius = 2.0,
						},
					},
			};
			PrimitiveWorldGenerationResult generation{};
			constexpr std::array chunks{ ChunkCoord{} };
			CpuMeshBatch initialBatch{};
			std::unique_ptr<PendingCpuMeshBatch> pending;
			std::unique_ptr<PreparedCpuMeshPublication> publication;
			if (GeneratePrimitiveVoxelWorld(config,
				std::span<const PrimitiveDesc>(&sphere, 1), world, generation).Failed() ||
				!world || BuildCpuMeshBatch(*world, 1, chunks, initialBatch).Failed() ||
				ValidateCpuMeshBatch(initialBatch, visible, pending).Failed() ||
				PrepareCpuMeshBatchPublication(pending, visible, publication).Failed())
			{
				return false;
			}
			CommitCpuMeshBatchPublication(publication, visible);

			const SphereEditRequest edit{
				.m_Brush = {
					.m_CenterWorld = { 4.0, 4.0, 4.0 },
					.m_Radius = 2.0,
					.m_Strength = 0.5,
					},
			};
			return ApplySphereEdit(*world, edit, mutation).Succeeded() &&
				mutation.GetChangeKind() == VoxelMutationChangeKind::DamageOnly;
		}

		void RunDataOnlyPublicationContractTests(TestContext& context) noexcept
		{
			using namespace napa::voxel;

			std::unique_ptr<VoxelWorld> world;
			VisibleMeshSet visible{};
			VoxelMutationResult mutation{};
			if (!BuildCoreDataOnlyPublicationInput(world, visible, mutation))
			{
				context.Check(false, "Data-only publication fixture builds a Damage-only mutation");
				return;
			}

			const auto chunksBefore = visible.GetChunks();
			const ChunkMeshRecord* chunkStorageBefore = chunksBefore.data();
			const WorldMeshValidationResult validationBefore = visible.GetWorldMeshValidation();
			const BoundaryContourValidationResult boundaryBefore =
				visible.GetBoundaryValidation();

			std::unique_ptr<PendingDataOnlyPublication> pending;
			const ValidationResult prepared = PrepareDataOnlyPublication(
				*world, mutation, visible, pending);
			PendingDataOnlyPublication* preservedPending = pending.get();
			VoxelMutationResult invalidSurface = mutation;
			const std::uint8_t originalDensity =
				invalidSurface.m_SampleChanges.front().m_After.m_Density;
			invalidSurface.m_SampleChanges.front().m_After.m_Density =
				originalDensity == std::numeric_limits<std::uint8_t>::max()
				? static_cast<std::uint8_t>(originalDensity - 1)
				: static_cast<std::uint8_t>(originalDensity + 1);
			const ValidationResult surfaceRejected = PrepareDataOnlyPublication(
				*world, invalidSurface, visible, pending);
			VoxelMutationResult invalidDirty = mutation;
			invalidDirty.m_DataDirtyChunks.clear();
			const ValidationResult dirtyRejected = PrepareDataOnlyPublication(
				*world, invalidDirty, visible, pending);
			VoxelMutationResult invalidBase = mutation;
			invalidBase.m_BaseWorldVoxelRevision =
				invalidBase.m_TargetWorldVoxelRevision;
			const ValidationResult baseRejected = PrepareDataOnlyPublication(
				*world, invalidBase, visible, pending);
			VoxelMutationResult invalidTarget = mutation;
			++invalidTarget.m_TargetWorldVoxelRevision;
			const ValidationResult targetRejected = PrepareDataOnlyPublication(
				*world, invalidTarget, visible, pending);
			VoxelMutationResult invalidAfter = mutation;
			invalidAfter.m_SampleChanges.front().m_After.m_Damage =
				static_cast<std::uint8_t>(
					invalidAfter.m_SampleChanges.front().m_After.m_Damage - 1);
			const ValidationResult afterRejected = PrepareDataOnlyPublication(
				*world, invalidAfter, visible, pending);
			VoxelMutationResult invalidMeshDirty = mutation;
			invalidMeshDirty.m_MeshDirtyChunks.push_back({});
			const ValidationResult meshDirtyRejected = PrepareDataOnlyPublication(
				*world, invalidMeshDirty, visible, pending);
			VoxelMutationResult emptyMutation{
				.m_BaseWorldVoxelRevision = mutation.m_BaseWorldVoxelRevision,
				.m_TargetWorldVoxelRevision = mutation.m_TargetWorldVoxelRevision,
			};
			const ValidationResult emptyRejected = PrepareDataOnlyPublication(
				*world, emptyMutation, visible, pending);
			context.Check(prepared.Succeeded() && pending && preservedPending == pending.get() &&
				surfaceRejected.m_Error == ValidationError::InvalidDataOnlyPublication &&
				dirtyRejected.m_Error == ValidationError::MismatchedDataOnlyPublication &&
				baseRejected.m_Error == ValidationError::StaleDataOnlyPublication &&
				targetRejected.m_Error == ValidationError::MismatchedDataOnlyPublication &&
				afterRejected.m_Error == ValidationError::MismatchedDataOnlyPublication &&
				meshDirtyRejected.m_Error == ValidationError::MismatchedDataOnlyPublication &&
				emptyRejected.m_Error == ValidationError::InvalidDataOnlyPublication,
				"Data-only preparation rejects Surface, Dirty, Base, Target, and "
				"authoritative-sample mismatches without replacing a valid token");

			CommitDataOnlyPublication(pending, visible);
			context.Check(!pending && visible.GetVisibleWorldRevision() ==
				mutation.m_TargetWorldVoxelRevision &&
				visible.GetChunks().data() == chunkStorageBefore &&
				visible.GetChunks().size() == chunksBefore.size() &&
				visible.GetWorldMeshValidation() == validationBefore &&
				visible.GetBoundaryValidation() == boundaryBefore,
				"Data-only commit advances only the visible revision and preserves immutable mesh evidence");

			std::unique_ptr<VoxelWorld> maximumWorld;
			VisibleMeshSet maximumVisible{};
			VoxelMutationResult maximumMutation{};
			const bool maximumFixture = BuildCoreDataOnlyPublicationInput(
				maximumWorld, maximumVisible, maximumMutation);
			maximumMutation.m_TargetWorldVoxelRevision =
				std::numeric_limits<std::uint64_t>::max();
			std::unique_ptr<PendingDataOnlyPublication> maximumPending;
			const ValidationResult maximumPrepared = maximumFixture
				? testing::PrepareWithAuthoritativeRevision(
					*maximumWorld, maximumMutation, maximumVisible,
					std::numeric_limits<std::uint64_t>::max(), maximumPending)
				: ValidationResult{ ValidationError::InvalidDataOnlyPublication };
			if (maximumPrepared.Succeeded())
			{
				CommitDataOnlyPublication(maximumPending, maximumVisible);
			}
			context.Check(maximumPrepared.Succeeded() && !maximumPending &&
				maximumVisible.GetVisibleWorldRevision() ==
				std::numeric_limits<std::uint64_t>::max(),
				"Data-only publication accepts a maximum Target revision without requiring another increment");

			std::unique_ptr<VoxelWorld> allocationWorld;
			VisibleMeshSet allocationVisible{};
			VoxelMutationResult allocationMutation{};
			const bool allocationFixture = BuildCoreDataOnlyPublicationInput(
				allocationWorld, allocationVisible, allocationMutation);
			std::unique_ptr<PendingDataOnlyPublication> allocationPending;
			const ValidationResult allocationFailure = allocationFixture
				? testing::PrepareWithAllocationFailure(
					*allocationWorld, allocationMutation, allocationVisible,
					allocationPending)
				: ValidationResult{ ValidationError::InvalidDataOnlyPublication };
			context.Check(allocationFailure.m_Error ==
				ValidationError::DataOnlyPublicationAllocationFailure && !allocationPending &&
				allocationVisible.GetVisibleWorldRevision() ==
				allocationMutation.m_BaseWorldVoxelRevision,
				"Data-only token allocation failure preserves the complete visible state and output");

			std::unique_ptr<VoxelWorld> forgedWorld;
			VisibleMeshSet forgedVisible{};
			VoxelMutationResult incompleteMutation{};
			const bool forgedFixture = BuildCoreDataOnlyPublicationInput(
				forgedWorld, forgedVisible, incompleteMutation);
			bool surfaceChanged = false;
			const ValidationResult surfaceWrite = forgedFixture
				? forgedWorld->WriteCurrentSample({ 0, 0, 0 }, {
						.m_Density = IsoValue,
						.m_Material = VoxelMaterial::Stone,
						.m_Damage = 0,
					}, surfaceChanged)
					: ValidationResult{ ValidationError::InvalidDataOnlyPublication };
			if (surfaceWrite.Succeeded())
			{
				incompleteMutation.m_TargetWorldVoxelRevision =
					forgedWorld->GetWorldVoxelRevision();
			}
			std::unique_ptr<PendingDataOnlyPublication> forgedPending;
			const ValidationResult forgedResult = surfaceWrite.Succeeded()
				? PrepareDataOnlyPublication(
					*forgedWorld, incompleteMutation, forgedVisible, forgedPending)
				: surfaceWrite;
			context.Check(surfaceChanged && forgedResult.m_Error ==
				ValidationError::MismatchedDataOnlyPublication && !forgedPending &&
				forgedWorld->GetSurfaceStateRevision() !=
				forgedVisible.GetSurfaceStateRevision(),
				"Data-only preparation rejects an incomplete Mutation that hides a newer Surface state");
		}
	}

	void RunNapaVoxelDataOnlyPublicationSelfTests(TestContext& context) noexcept
	{
		RunDataOnlyPublicationContractTests(context);
	}
}
