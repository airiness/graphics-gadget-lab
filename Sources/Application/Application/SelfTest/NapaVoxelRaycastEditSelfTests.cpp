#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "Application/Lab/NapaVoxel/NapaVoxelRaycast.h"
#include "NapaVoxelCore/Edit/VoxelMutation.h"
#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"
#include "NapaVoxelCore/Meshing/DataOnlyPublication.h"
#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"
#include "NapaVoxelCore/World/VoxelWorld.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] napa::voxel::ValidationResult PrepareAndCommitMutationMeshBatch(
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pending,
			napa::voxel::VisibleMeshSet& visible)
		{
			using namespace napa::voxel;
			std::unique_ptr<PreparedCpuMeshPublication> publication;
			const ValidationResult prepareResult =
				PrepareCpuMeshBatchPublication(pending, visible, publication);
			if (prepareResult.Succeeded())
			{
				CommitCpuMeshBatchPublication(publication, visible);
			}
			return prepareResult;
		}

		[[nodiscard]] bool RunRepeatedStoneRaycastEditScenario(
			const napa::voxel::VoxelWorldConfig& config,
			const napa::voxel::PrimitiveDesc& primitive, const NapaVoxelRay& ray)
		{
			using namespace napa::voxel;
			LogicalDomainMetrics metrics{};
			if (ComputeLogicalDomainMetrics(config, metrics).Failed())
			{
				return false;
			}
			std::vector<ChunkCoord> chunks;
			chunks.reserve(static_cast<std::size_t>(metrics.m_CellOwnerChunkCount));
			for (std::int32_t z = metrics.m_CellOwnerChunkBounds.m_Min.m_Z;
				z < metrics.m_CellOwnerChunkBounds.m_MaxExclusive.m_Z; ++z)
			{
				for (std::int32_t y = metrics.m_CellOwnerChunkBounds.m_Min.m_Y;
					y < metrics.m_CellOwnerChunkBounds.m_MaxExclusive.m_Y; ++y)
				{
					for (std::int32_t x = metrics.m_CellOwnerChunkBounds.m_Min.m_X;
						x < metrics.m_CellOwnerChunkBounds.m_MaxExclusive.m_X; ++x)
					{
						chunks.push_back({ x, y, z });
					}
				}
			}

			std::unique_ptr<VoxelWorld> world;
			PrimitiveWorldGenerationResult generation{};
			VisibleMeshSet visible;
			CpuMeshBatch initialBatch{};
			std::unique_ptr<PendingCpuMeshBatch> initialPending;
			bool completed = GeneratePrimitiveVoxelWorld(
				config, std::span{ &primitive, 1 }, world, generation).Succeeded() && world &&
				BuildCpuMeshBatch(*world, 1, chunks, initialBatch).Succeeded() &&
				ValidateCpuMeshBatch(initialBatch, visible, initialPending).Succeeded() &&
				PrepareAndCommitMutationMeshBatch(initialPending, visible).Succeeded();
			for (std::uint32_t hit = 1; completed && hit <= 3; ++hit)
			{
				NapaVoxelRaycastHit raycastHit{};
				const NapaVoxelRaycastResult raycast =
					RaycastNapaVoxelVisibleMesh(visible, ray, raycastHit);
				if (raycast.Failed() || !raycast.m_Hit)
				{
					completed = false;
					break;
				}
				const SphereEditRequest edit{
					.m_Brush = {
						.m_CenterWorld = raycastHit.m_WorldPosition,
						.m_Radius = 1.25,
						.m_Strength = 1.0,
					},
					.m_MaterialRules = {
						.m_DamagePerHit = 128,
						.m_StoneBreakThreshold = 255,
					},
				};
				VoxelMutationResult mutation{};
				ValidationResult result = ApplySphereEdit(*world, edit, mutation);
				if (result.Failed())
				{
					completed = false;
					break;
				}
				if (!mutation.Changed())
				{
					break;
				}
				if (mutation.GetChangeKind() == VoxelMutationChangeKind::DamageOnly)
				{
					std::unique_ptr<PendingDataOnlyPublication> pending;
					result = PrepareDataOnlyPublication(*world, mutation, visible, pending);
					if (result.Succeeded())
					{
						CommitDataOnlyPublication(pending, visible);
					}
				}
				else
				{
					CpuMeshBatch batch{};
					std::unique_ptr<PendingCpuMeshBatch> pending;
					result = BuildCpuMeshBatch(*world, mutation, batch);
					if (result.Succeeded())
					{
						result = ValidateCpuMeshBatch(batch, visible, pending);
					}
					if (result.Succeeded())
					{
						result = PrepareAndCommitMutationMeshBatch(pending, visible);
					}
				}
				if (result.Failed())
				{
					completed = false;
				}
			}
			return completed && world && world->GetWorldVoxelRevision() == 4 &&
				visible.GetVisibleWorldRevision() == 4;
		}

		void RunBoundarySensitiveRepeatedStoneEditTests(SelfTestContext& context)
		{
			using namespace napa::voxel;
			const auto makeStone = [](Double3 center) noexcept
				{
					return PrimitiveDesc{
						.m_StableId = { 1 },
						.m_Material = VoxelMaterial::Stone,
						.m_Shape = PrimitiveShape::Sphere,
						.m_Parameters = {
							.m_Sphere = {
								.m_Center = center,
								.m_Radius = 0.96,
							},
						},
					};
				};
			const VoxelWorldConfig boundaryConfig{
				.m_ChunkCellCount = 16,
				.m_VoxelSize = 0.25f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = {},
					.m_MaxExclusive = { 32, 32, 32 },
				},
			};
			const VoxelWorldConfig negativeConfig{
				.m_ChunkCellCount = 16,
				.m_VoxelSize = 0.25f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = { -32, -16, 16 },
					.m_MaxExclusive = { 0, 16, 32 },
				},
			};
			constexpr Double3 rayDirection{
				0.0, -0.1375683712746877, 0.9904922731777516,
			};
			context.Check(RunRepeatedStoneRaycastEditScenario(boundaryConfig,
				makeStone({ 4.0, 4.0, 4.0 }), {
					.m_Origin = { 4.0, 6.0, -10.4 },
					.m_Direction = rayDirection,
				}), "Boundary Corner repeated raycast edits publish through revision 4");
			context.Check(RunRepeatedStoneRaycastEditScenario(negativeConfig,
				makeStone({ -4.0, 0.0, 6.0 }), {
					.m_Origin = { -4.0, 2.0, -8.4 },
					.m_Direction = rayDirection,
				}), "Negative Chunk repeated raycast edits publish through revision 4");
		}
	}

	void RunNapaVoxelRaycastEditSelfTests(SelfTestContext& context) noexcept
	{
		RunBoundarySensitiveRepeatedStoneEditTests(context);
	}
}
