#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Meshing/ReferenceMesher.h"
#include "NapaVoxelCore/Meshing/WorldMeshHash.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] napa::voxel::VoxelWorldConfig
			MakeEightChunkConfig() noexcept
		{
			return {
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = { -8, -8, -8 },
					.m_MaxExclusive = { 8, 8, 8 },
				},
			};
		}

		[[nodiscard]] napa::voxel::VoxelWorldConfig
			MakeGuardChunkConfig() noexcept
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

		[[nodiscard]] napa::voxel::PrimitiveDesc MakeSphere(
			std::uint64_t stableId,
			napa::voxel::Double3 center,
			double radius) noexcept
		{
			using namespace napa::voxel;

			return {
				.m_StableId = { stableId },
				.m_Material = VoxelMaterial::Stone,
				.m_Shape = PrimitiveShape::Sphere,
				.m_Parameters = {
					.m_Sphere = {
						.m_Center = center,
						.m_Radius = radius,
					},
				},
			};
		}

		[[nodiscard]] bool GenerateWorld(
			const napa::voxel::VoxelWorldConfig& config,
			std::span<const napa::voxel::PrimitiveDesc> primitives,
			std::unique_ptr<napa::voxel::VoxelWorld>& world)
		{
			napa::voxel::PrimitiveWorldGenerationResult generation{};
			return
				napa::voxel::GeneratePrimitiveVoxelWorld(
					config,
					primitives,
					world,
					generation).Succeeded() &&
				world != nullptr;
		}

		[[nodiscard]] bool IsCanonicalEightChunkOrder(
			std::span<const napa::voxel::ChunkMeshRecord> records)
			noexcept
		{
			using namespace napa::voxel;

			constexpr std::array expected{
				ChunkCoord{ -1, -1, -1 },
				ChunkCoord{ 0, -1, -1 },
				ChunkCoord{ -1, 0, -1 },
				ChunkCoord{ 0, 0, -1 },
				ChunkCoord{ -1, -1, 0 },
				ChunkCoord{ 0, -1, 0 },
				ChunkCoord{ -1, 0, 0 },
				ChunkCoord{ 0, 0, 0 },
			};
			if (records.size() != expected.size())
			{
				return false;
			}
			for (std::size_t index = 0;
				index < records.size();
				++index)
			{
				if (records[index].m_Chunk != expected[index])
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool IsCanonicalEmptyWorldMesh(
			const napa::voxel::ReferenceWorldMeshingResult& meshing)
			noexcept
		{
			for (const napa::voxel::ChunkMeshRecord& record :
				meshing.m_Chunks)
			{
				if (record.m_SourceWorldVoxelRevision != 1 ||
					!record.m_Mesh.m_Vertices.empty() ||
					!record.m_Mesh.m_Sections.empty() ||
					record.m_Validation.m_VertexCount != 0 ||
					record.m_Validation.m_SectionCount != 0 ||
					record.m_Validation.m_IndexCount != 0 ||
					record.m_Validation.m_TriangleCount != 0 ||
					record.m_SkippedDegenerateTriangleCount != 0)
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool TouchesSharedPlanesAndEdges(
			const napa::voxel::ChunkMeshRecord& record) noexcept
		{
			using namespace napa::voxel;

			const FloatAabb& bounds = record.m_Mesh.m_Bounds;
			const float sharedX =
				record.m_Chunk.m_X < 0 ? 8.0f : 0.0f;
			const float sharedY =
				record.m_Chunk.m_Y < 0 ? 8.0f : 0.0f;
			const float sharedZ =
				record.m_Chunk.m_Z < 0 ? 8.0f : 0.0f;
			const bool touchesPlanes =
				(record.m_Chunk.m_X < 0
					? bounds.m_Max.m_X == sharedX
					: bounds.m_Min.m_X == sharedX) &&
				(record.m_Chunk.m_Y < 0
					? bounds.m_Max.m_Y == sharedY
					: bounds.m_Min.m_Y == sharedY) &&
				(record.m_Chunk.m_Z < 0
					? bounds.m_Max.m_Z == sharedZ
					: bounds.m_Min.m_Z == sharedZ);
			if (!touchesPlanes)
			{
				return false;
			}

			for (const MeshVertex& vertex :
				record.m_Mesh.m_Vertices)
			{
				const std::uint32_t sharedPlaneCount =
					static_cast<std::uint32_t>(
						vertex.m_Position.m_X == sharedX) +
					static_cast<std::uint32_t>(
						vertex.m_Position.m_Y == sharedY) +
					static_cast<std::uint32_t>(
						vertex.m_Position.m_Z == sharedZ);
				if (sharedPlaneCount >= 2)
				{
					return true;
				}
			}
			return false;
		}

		void RunCompleteDomainTests(SelfTestContext& context)
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config =
				MakeEightChunkConfig();
			std::unique_ptr<VoxelWorld> world;
			const bool generated = GenerateWorld(
				config,
				{},
				world);
			ReferenceWorldMeshingResult meshing{};
			const bool meshed =
				generated &&
				ReferenceMesher(*world).MeshWorld(
					meshing).Succeeded();

			context.Check(
				meshed &&
					IsCanonicalEightChunkOrder(
						meshing.m_Chunks) &&
					IsCanonicalEmptyWorldMesh(meshing) &&
					meshing.m_Validation.m_ChunkCount == 8 &&
					meshing.m_Validation.m_VertexCount == 0 &&
					meshing.m_Validation.m_SectionCount == 0 &&
					meshing.m_Validation.m_IndexCount == 0 &&
					meshing.m_Validation.m_TriangleCount == 0 &&
					meshing.m_Validation.m_ValidationHash ==
						0x572bf6dcaaab0aa0ull,
				"World meshing traverses every Cell-owner Chunk in canonical order");
			if (!meshed)
			{
				return;
			}

			const WorldMeshValidationResult sentinel{
				.m_ValidationHash =
					0x123456789abcdef0ull,
				.m_ChunkCount = 1,
				.m_VertexCount = 2,
				.m_SectionCount = 3,
				.m_IndexCount = 4,
				.m_TriangleCount = 5,
				.m_SkippedDegenerateTriangleCount = 6,
			};
			WorldMeshValidationResult unchanged = sentinel;
			context.Check(
				ValidateAndHashWorldMeshRecords(
					std::span(meshing.m_Chunks).first(7),
					config,
					unchanged).m_Error ==
						ValidationError::
							InvalidWorldMeshRecordSet &&
					unchanged == sentinel,
				"World mesh hashing rejects incomplete Chunk Domains atomically");

			std::vector<ChunkMeshRecord> reordered =
				meshing.m_Chunks;
			std::swap(reordered[0], reordered[1]);
			context.Check(
				ValidateAndHashWorldMeshRecords(
					reordered,
					config,
					unchanged).m_Error ==
						ValidationError::
							InvalidWorldMeshRecordSet &&
					unchanged == sentinel,
				"World mesh hashing requires canonical z-y-x record order");

			std::vector<ChunkMeshRecord> duplicated =
				meshing.m_Chunks;
			duplicated[0] = duplicated[1];
			context.Check(
				ValidateAndHashWorldMeshRecords(
					duplicated,
					config,
					unchanged).m_Error ==
						ValidationError::
							InvalidWorldMeshRecordSet &&
					unchanged == sentinel,
				"World mesh hashing requires a complete unique Chunk Domain");

			std::vector<ChunkMeshRecord> mismatched =
				meshing.m_Chunks;
			mismatched[0].m_Validation.m_ValidationHash ^= 1;
			context.Check(
				ValidateAndHashWorldMeshRecords(
					mismatched,
					config,
					unchanged).m_Error ==
						ValidationError::
							MismatchedChunkMeshValidation &&
					unchanged == sentinel,
				"World mesh hashing revalidates stored Chunk evidence");
		}

		void RunBoundarySurfaceTests(SelfTestContext& context)
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config =
				MakeEightChunkConfig();
			const std::array primitives{
				MakeSphere(1, {}, 3.0),
			};
			std::unique_ptr<VoxelWorld> world;
			const bool generated = GenerateWorld(
				config,
				primitives,
				world);
			VoxelSample sharedCorner{};
			ReferenceWorldMeshingResult meshing{};
			const bool meshed =
				generated &&
				world->ReadCurrentSample(
					{},
					sharedCorner).Succeeded() &&
				ReferenceMesher(*world).MeshWorld(
					meshing).Succeeded();

			bool completeBoundaryCoverage =
				meshed &&
				IsCanonicalEightChunkOrder(
					meshing.m_Chunks) &&
				sharedCorner.m_Density >= IsoValue &&
				sharedCorner.m_Material ==
					VoxelMaterial::Stone;
			if (completeBoundaryCoverage)
			{
				for (const ChunkMeshRecord& record :
					meshing.m_Chunks)
				{
					completeBoundaryCoverage =
						record.m_SourceWorldVoxelRevision == 1 &&
						!record.m_Mesh.m_Vertices.empty() &&
						record.m_Mesh.m_Sections.size() == 1 &&
						record.m_Mesh.m_Sections[0].m_Material ==
							VoxelMaterial::Stone &&
						TouchesSharedPlanesAndEdges(record);
					if (!completeBoundaryCoverage)
					{
						break;
					}
				}
			}
			context.Check(
				completeBoundaryCoverage,
				"Face-edge-corner surfaces mesh across positive halos and negative Chunks");
			if (!meshed)
			{
				return;
			}
			context.Check(
				meshing.m_Validation ==
					WorldMeshValidationResult{
						.m_ValidationHash =
							0xb939dfe96d74fe89ull,
						.m_ChunkCount = 8,
						.m_VertexCount = 1908,
						.m_SectionCount = 8,
						.m_IndexCount = 1908,
						.m_TriangleCount = 636,
						.m_SkippedDegenerateTriangleCount =
							444,
					},
				"A boundary sphere matches the complete World mesh golden");

			bool deterministic = true;
			for (std::uint32_t iteration = 0;
				iteration < 10 && deterministic;
				++iteration)
			{
				ReferenceWorldMeshingResult repeated{};
				deterministic =
					ReferenceMesher(*world).MeshWorld(
						repeated).Succeeded() &&
					repeated.m_Validation ==
						meshing.m_Validation;
			}
			context.Check(
				deterministic,
				"Repeated multi-Chunk meshing preserves canonical totals and hash");
		}

		void RunGuardAllocationTests(SelfTestContext& context)
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config =
				MakeGuardChunkConfig();
			const std::array primitives{
				MakeSphere(1, { 4.0, 4.0, 4.0 }, 1.5),
			};
			std::unique_ptr<VoxelWorld> baselineWorld;
			std::unique_ptr<VoxelWorld> guardWorld;
			const bool generated =
				GenerateWorld(
					config,
					primitives,
					baselineWorld) &&
				GenerateWorld(
					config,
					primitives,
					guardWorld);
			const std::size_t baselineResidentCount =
				generated
					? guardWorld->GetResidentChunkCount()
					: 0;
			bool allocated = false;
			ReferenceWorldMeshingResult baselineMeshing{};
			ReferenceWorldMeshingResult guardMeshing{};
			const bool meshed =
				generated &&
				guardWorld->EnsureChunkAllocated(
					{ 1, 1, 1 },
					allocated).Succeeded() &&
				allocated &&
				guardWorld->GetResidentChunkCount() ==
					baselineResidentCount + 1 &&
				ReferenceMesher(*baselineWorld).MeshWorld(
					baselineMeshing).Succeeded() &&
				ReferenceMesher(*guardWorld).MeshWorld(
					guardMeshing).Succeeded();
			context.Check(
				meshed &&
					baselineMeshing.m_Chunks.size() == 1 &&
					guardMeshing.m_Chunks.size() == 1 &&
					guardMeshing.m_Chunks[0].m_Chunk ==
						ChunkCoord{} &&
					guardMeshing.m_Validation ==
						baselineMeshing.m_Validation,
				"Guard Sample allocation creates no Cell mesh and changes no World mesh hash");
		}
	}

	void RunNapaVoxelMultiChunkSelfTests(
		SelfTestContext& context) noexcept
	{
		RunCompleteDomainTests(context);
		RunBoundarySurfaceTests(context);
		RunGuardAllocationTests(context);
	}
}
