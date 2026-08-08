#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "Application/Lab/NapaVoxel/NapaVoxelCommands.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRaycast.h"

#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] napa::voxel::VoxelWorldConfig MakeRaycastConfig() noexcept
		{
			return {
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = { -8, 0, 0 },
					.m_MaxExclusive = { 8, 8, 8 },
				},
			};
		}

		[[nodiscard]] napa::voxel::SphereEditRequest MakeCommandEdit(
			double radius, double strength, std::uint8_t damagePerHit) noexcept
		{
			return {
				.m_Brush = {
					.m_Radius = radius,
					.m_Strength = strength,
				},
				.m_MaterialRules = {
					.m_DamagePerHit = damagePerHit,
					.m_StoneBreakThreshold = 255,
				},
			};
		}

		[[nodiscard]] napa::voxel::ChunkMeshRecord MakeRaycastRecord(
			napa::voxel::ChunkCoord chunk, napa::voxel::VoxelMaterial material,
			std::array<napa::voxel::Float3, 3> positions)
		{
			using namespace napa::voxel;
			ChunkMeshRecord record{
				.m_Chunk = chunk,
				.m_SourceWorldVoxelRevision = 1,
			};
			for (const Float3 position : positions)
			{
				record.m_Mesh.m_Vertices.push_back({
					.m_Position = position,
					.m_Normal = { 0.0f, 0.0f, 1.0f },
					});
			}
			record.m_Mesh.m_Sections.push_back({
				.m_Material = material,
				.m_Indices = { 0, 1, 2 },
				});
			return record;
		}

		[[nodiscard]] std::vector<napa::voxel::ChunkMeshRecord> MakeEdgeTieRecords()
		{
			using namespace napa::voxel;
			std::vector<ChunkMeshRecord> records;
			records.push_back(MakeRaycastRecord({ -1, 0, 0 }, VoxelMaterial::Stone, {
				Float3{ 8.0f, 0.0f, 1.0f },
				Float3{ 8.0f, 1.0f, 1.0f },
				Float3{ 7.0f, 0.0f, 1.0f },
				}));
			records.push_back(MakeRaycastRecord({}, VoxelMaterial::Soil, {
				Float3{ 0.0f, 0.0f, 1.0f },
				Float3{ 1.0f, 0.0f, 1.0f },
				Float3{ 0.0f, 1.0f, 1.0f },
				}));
			return records;
		}

		[[nodiscard]] bool CreateVisibleRaycastFixture(
			napa::voxel::VisibleMeshSet& visible)
		{
			using namespace napa::voxel;
			VoxelWorldConfig config = MakeRaycastConfig();
			config.m_LogicalCellBounds.m_Min = {};
			const PrimitiveDesc primitive{
				.m_StableId = { 1 },
				.m_Material = VoxelMaterial::Stone,
				.m_Shape = PrimitiveShape::AxisAlignedBox,
				.m_Parameters = {
					.m_AxisAlignedBox = {
						.m_Center = { 4.0, 4.0, 4.0 },
						.m_HalfExtents = { 1.0, 1.0, 1.0 },
					},
				},
			};
			std::unique_ptr<VoxelWorld> world;
			PrimitiveWorldGenerationResult generation{};
			CpuMeshBatch batch{};
			std::unique_ptr<PendingCpuMeshBatch> pending;
			std::unique_ptr<PreparedCpuMeshPublication> publication;
			const std::array chunks{ ChunkCoord{} };
			if (GeneratePrimitiveVoxelWorld(config, std::span{ &primitive, 1 },
				world, generation).Failed() || !world ||
				BuildCpuMeshBatch(*world, world->GetWorldVoxelRevision(), chunks, batch).Failed() ||
				ValidateCpuMeshBatch(batch, visible, pending).Failed() || !pending ||
				PrepareCpuMeshBatchPublication(pending, visible, publication).Failed() ||
				pending || !publication)
			{
				return false;
			}
			CommitCpuMeshBatchPublication(publication, visible);
			return !publication && visible.HasPublishedMeshes();
		}

		void RunRaycastTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			const VoxelWorldConfig config = MakeRaycastConfig();
			const std::vector<ChunkMeshRecord> records = MakeEdgeTieRecords();
			std::vector<ChunkMeshRecord> reversed = records;
			std::reverse(reversed.begin(), reversed.end());
			const NapaVoxelRay edgeRay{
				.m_Origin = { 0.0, 0.25, 0.0 },
				.m_Direction = { 0.0, 0.0, 7.0 },
			};
			NapaVoxelRaycastHit forwardHit{};
			NapaVoxelRaycastHit reverseHit{};
			const NapaVoxelRaycastResult forward =
				RaycastNapaVoxelMeshRecords(config, records, edgeRay, forwardHit);
			const NapaVoxelRaycastResult reverse =
				RaycastNapaVoxelMeshRecords(config, reversed, edgeRay, reverseHit);
			context.Check(forward.Succeeded() && forward.m_Hit && reverse.Succeeded() &&
				reverse.m_Hit && forwardHit == reverseHit &&
				forwardHit.m_Chunk == ChunkCoord{ -1, 0, 0 } &&
				forwardHit.m_Material == VoxelMaterial::Stone &&
				forwardHit.m_Distance == 1.0 && forwardHit.m_DistanceKey == 1'000'000'000,
				"Visible mesh Raycast uses a canonical edge tie-break independent of input order");

			const NapaVoxelRay vertexRay{
				.m_Origin = {},
				.m_Direction = { 0.0, 0.0, 1.0 },
			};
			NapaVoxelRaycastHit vertexHit{};
			context.Check(RaycastNapaVoxelMeshRecords(
				config, records, vertexRay, vertexHit).m_Hit &&
				vertexHit.m_Chunk == ChunkCoord{ -1, 0, 0 } &&
				vertexHit.m_WorldPosition == Double3{ 0.0, 0.0, 1.0 },
				"Visible mesh Raycast resolves a shared vertex with the same total order");

			std::vector<ChunkMeshRecord> overlappingRecords{
				MakeRaycastRecord({}, VoxelMaterial::Stone, {
					Float3{ 0.0f, 0.0f, 1.0f },
					Float3{ 1.0f, 0.0f, 1.0f },
					Float3{ 0.0f, 1.0f, 1.0f },
					}),
				MakeRaycastRecord({}, VoxelMaterial::Soil, {
					Float3{ 0.0f, 0.0f, 1.0f },
					Float3{ 1.0f, 0.0f, 1.0f },
					Float3{ 0.0f, 1.0f, 1.0f },
					}),
			};
			NapaVoxelRaycastHit overlappingForwardHit{};
			NapaVoxelRaycastHit overlappingReverseHit{};
			const NapaVoxelRay overlappingRay{
				.m_Origin = { 0.25, 0.25, 0.0 },
				.m_Direction = { 0.0, 0.0, 1.0 },
			};
			const bool overlappingForward = RaycastNapaVoxelMeshRecords(
				config, overlappingRecords, overlappingRay, overlappingForwardHit).m_Hit;
			std::reverse(overlappingRecords.begin(), overlappingRecords.end());
			const bool overlappingReverse = RaycastNapaVoxelMeshRecords(
				config, overlappingRecords, overlappingRay, overlappingReverseHit).m_Hit;
			context.Check(overlappingForward && overlappingReverse &&
				overlappingForwardHit == overlappingReverseHit &&
				overlappingForwardHit.m_Material == VoxelMaterial::Soil,
				"Visible mesh Raycast orders overlapping coplanar Triangles by canonical material");

			const NapaVoxelRaycastHit sentinel{
				.m_WorldPosition = { 91.0, 92.0, 93.0 },
				.m_Distance = 94.0,
				.m_DistanceKey = 95,
				.m_Chunk = { 96, 97, 98 },
				.m_Material = VoxelMaterial::Stone,
				.m_SectionOrdinal = 99,
				.m_TriangleOrdinal = 100,
			};
			NapaVoxelRaycastHit unchanged = sentinel;
			const NapaVoxelRay missRay{
				.m_Origin = { 4.0, 4.0, 0.0 },
				.m_Direction = { 0.0, 0.0, 1.0 },
			};
			const NapaVoxelRaycastResult miss =
				RaycastNapaVoxelMeshRecords(config, records, missRay, unchanged);
			context.Check(miss.Succeeded() && !miss.m_Hit && unchanged == sentinel,
				"A visible mesh Raycast miss preserves its output");

			const NapaVoxelRay invalidRay{
				.m_Origin = {},
				.m_Direction = {},
			};
			context.Check(RaycastNapaVoxelMeshRecords(
				config, records, invalidRay, unchanged).m_Error ==
				NapaVoxelRaycastError::InvalidRay && unchanged == sentinel,
				"Visible mesh Raycast rejects a zero direction without changing output");
			const NapaVoxelRay nonFiniteRay{
				.m_Origin = {},
				.m_Direction = {
					std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0,
				},
			};
			context.Check(RaycastNapaVoxelMeshRecords(
				config, records, nonFiniteRay, unchanged).m_Error ==
				NapaVoxelRaycastError::InvalidRay && unchanged == sentinel,
				"Visible mesh Raycast rejects a non-finite direction atomically");

			const NapaVoxelRay overflowingRay{
				.m_Origin = { 0.0, 0.25, -1.0e30 },
				.m_Direction = { 0.0, 0.0, 1.0 },
			};
			const NapaVoxelRaycastResult overflowing =
				RaycastNapaVoxelMeshRecords(config, records, overflowingRay, unchanged);
			context.Check(overflowing.Succeeded() && !overflowing.m_Hit && unchanged == sentinel,
				"Raycast skips Candidates whose deterministic distance key overflows");

			VisibleMeshSet emptyVisible;
			context.Check(RaycastNapaVoxelVisibleMesh(emptyVisible, edgeRay, unchanged).m_Error ==
				NapaVoxelRaycastError::UninitializedVisibleMesh && unchanged == sentinel,
				"Raycast rejects an unpublished Visible Mesh Set atomically");

			VisibleMeshSet visible;
			NapaVoxelRaycastHit visibleHit{};
			context.Check(CreateVisibleRaycastFixture(visible) &&
				RaycastNapaVoxelVisibleMesh(visible, {
					.m_Origin = { 4.0, 4.0, 0.0 },
					.m_Direction = { 0.0, 0.0, 2.0 },
					}, visibleHit).m_Hit && visibleHit.m_Material == VoxelMaterial::Stone &&
					visibleHit.m_WorldPosition.m_Z == 3.0,
				"Raycast queries the currently published complete Visible Mesh Set");
		}

		void RunCommandQueueTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			NapaVoxelCommandQueue queue;
			NapaVoxelRay firstRay{
				.m_Origin = { 1.0, 2.0, 3.0 },
				.m_Direction = { 0.0, 0.0, 2.0 },
			};
			SphereEditRequest firstEdit = MakeCommandEdit(1.25, 0.5, 17);
			const NapaVoxelRay secondRay{
				.m_Origin = { -1.0, -2.0, -3.0 },
				.m_Direction = { 1.0, 0.0, 0.0 },
			};
			const SphereEditRequest secondEdit = MakeCommandEdit(2.5, 1.0, 29);
			const bool enqueued =
				queue.EnqueueFireRay(firstRay, firstEdit) == NapaVoxelCommandQueueError::None &&
				queue.EnqueueRestoreAll() == NapaVoxelCommandQueueError::None &&
				queue.EnqueueFireRay(secondRay, secondEdit) == NapaVoxelCommandQueueError::None &&
				queue.EnqueueRestoreProbeChunk({ -2, 3, -4 }) ==
				NapaVoxelCommandQueueError::None &&
				queue.EnqueueMoveProbeRay(secondRay) == NapaVoxelCommandQueueError::None &&
				queue.EnqueueScriptedBoundaryShot(secondEdit) ==
				NapaVoxelCommandQueueError::None;
			firstRay.m_Origin = { 100.0, 100.0, 100.0 };
			firstEdit.m_Brush.m_Radius = 100.0;

			std::array<NapaVoxelDequeuedCommand, 6> dequeued{};
			bool dequeuedAll = enqueued && queue.GetSize() == dequeued.size();
			for (NapaVoxelDequeuedCommand& command : dequeued)
			{
				dequeuedAll = dequeuedAll &&
					queue.Dequeue(command) == NapaVoxelCommandQueueError::None;
			}
			const auto* capturedFirst =
				std::get_if<NapaVoxelFireRayCommand>(&dequeued[0].m_Command.m_Data);
			const auto* capturedSecond =
				std::get_if<NapaVoxelFireRayCommand>(&dequeued[2].m_Command.m_Data);
			const auto* capturedChunk =
				std::get_if<NapaVoxelRestoreProbeChunkCommand>(&dequeued[3].m_Command.m_Data);
			context.Check(dequeuedAll && queue.IsEmpty() &&
				dequeued[0].m_OperationSerial == 1 && dequeued[5].m_OperationSerial == 6 &&
				dequeued[0].m_Command.m_EnqueueSerial == 1 &&
				dequeued[5].m_Command.m_EnqueueSerial == 6 && capturedFirst && capturedSecond &&
				capturedFirst->m_Ray.m_Origin == Double3{ 1.0, 2.0, 3.0 } &&
				capturedFirst->m_Edit.m_Brush.m_Radius == 1.25 &&
				capturedFirst->m_Edit.m_Brush.m_Strength == 0.5 &&
				capturedFirst->m_Edit.m_MaterialRules.m_DamagePerHit == 17 &&
				capturedSecond->m_Ray == secondRay &&
				dequeued[1].m_Command.GetType() == NapaVoxelCommandType::RestoreAll &&
				capturedChunk && capturedChunk->m_Chunk == ChunkCoord{ -2, 3, -4 } &&
				dequeued[4].m_Command.GetType() == NapaVoxelCommandType::MoveProbeRay &&
				dequeued[5].m_Command.GetType() == NapaVoxelCommandType::ScriptedBoundaryShot,
				"Session FIFO preserves command order, immutable Fire snapshots, and Restore data");

			NapaVoxelCommandQueue noOpQueue;
			const NapaVoxelRay missRay{
				.m_Origin = { 4.0, 4.0, 0.0 },
				.m_Direction = { 0.0, 0.0, 1.0 },
			};
			NapaVoxelDequeuedCommand noOpCommand{};
			NapaVoxelRaycastHit noOpHit{};
			const std::vector<ChunkMeshRecord> records = MakeEdgeTieRecords();
			const bool noOpDequeued = noOpQueue.EnqueueFireRay(missRay, secondEdit) ==
				NapaVoxelCommandQueueError::None && noOpQueue.Dequeue(noOpCommand) ==
				NapaVoxelCommandQueueError::None;
			const NapaVoxelRaycastResult noOpRaycast = noOpDequeued ?
				RaycastNapaVoxelMeshRecords(MakeRaycastConfig(), records, missRay, noOpHit) :
				NapaVoxelRaycastResult{ .m_Error = NapaVoxelRaycastError::InvalidRay };
			context.Check(noOpDequeued && noOpCommand.m_OperationSerial == 1 &&
				noOpRaycast.Succeeded() && !noOpRaycast.m_Hit &&
				noOpQueue.GetLastOperationSerial() == 1,
				"A Fire miss remains a FIFO no-op Operation with an allocated serial");

			NapaVoxelCommandQueue validationQueue;
			SphereEditRequest invalidEdit = secondEdit;
			invalidEdit.m_Brush.m_Radius = 0.0;
			context.Check(validationQueue.EnqueueFireRay({}, secondEdit) ==
				NapaVoxelCommandQueueError::InvalidRay &&
				validationQueue.EnqueueScriptedBoundaryShot(invalidEdit) ==
				NapaVoxelCommandQueueError::InvalidEdit && validationQueue.IsEmpty() &&
				validationQueue.GetLastEnqueueSerial() == 0,
				"Rejected commands allocate no Enqueue Serial and leave the FIFO unchanged");
		}

		void RunCommandSerialExhaustionTests(SelfTestContext& context) noexcept
		{
			const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
			NapaVoxelCommandQueue enqueueQueue({
				.m_LastEnqueueSerial = maximum - 1,
				});
			const bool finalEnqueue = enqueueQueue.EnqueueRestoreAll() ==
				NapaVoxelCommandQueueError::None;
			context.Check(finalEnqueue && enqueueQueue.GetLastEnqueueSerial() == maximum &&
				enqueueQueue.GetSize() == 1 && enqueueQueue.EnqueueRestoreAll() ==
				NapaVoxelCommandQueueError::EnqueueSerialExhausted && enqueueQueue.IsTerminal() &&
				enqueueQueue.GetTerminalError() ==
				NapaVoxelCommandQueueError::EnqueueSerialExhausted &&
				enqueueQueue.GetLastEnqueueSerial() == maximum && enqueueQueue.GetSize() == 1,
				"Enqueue Serial reaches its maximum once and then fails terminally without wrap");

			NapaVoxelCommandQueue operationQueue({
				.m_LastOperationSerial = maximum - 1,
				});
			NapaVoxelDequeuedCommand first{};
			const NapaVoxelDequeuedCommand sentinel{
				.m_OperationSerial = 91,
				.m_Command = {
					.m_EnqueueSerial = 92,
					.m_Data = NapaVoxelRestoreProbeChunkCommand{ { 93, 94, 95 } },
				},
			};
			NapaVoxelDequeuedCommand unchanged = sentinel;
			const bool queued = operationQueue.EnqueueRestoreAll() ==
				NapaVoxelCommandQueueError::None && operationQueue.EnqueueRestoreAll() ==
				NapaVoxelCommandQueueError::None;
			const bool finalOperation = queued && operationQueue.Dequeue(first) ==
				NapaVoxelCommandQueueError::None;
			context.Check(finalOperation && first.m_OperationSerial == maximum &&
				operationQueue.GetLastOperationSerial() == maximum &&
				operationQueue.Dequeue(unchanged) ==
				NapaVoxelCommandQueueError::OperationSerialExhausted &&
				operationQueue.IsTerminal() && operationQueue.GetTerminalError() ==
				NapaVoxelCommandQueueError::OperationSerialExhausted && unchanged == sentinel &&
				operationQueue.GetSize() == 1 &&
				operationQueue.GetLastOperationSerial() == maximum,
				"Operation Serial reaches its maximum once and preserves the queued command on exhaustion");
		}
	}

	void RunNapaVoxelCommandSelfTests(SelfTestContext& context) noexcept
	{
		RunRaycastTests(context);
		RunCommandQueueTests(context);
		RunCommandSerialExhaustionTests(context);
	}
}
