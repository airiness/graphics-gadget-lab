#include "NapaVoxelTestFramework.h"

#include "NapaVoxelCore/Edit/VoxelMutation.h"
#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Meshing/BoundaryContour.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"
#include "NapaVoxelCore/Meshing/ReferenceMesher.h"
#include "NapaVoxelCore/Meshing/WorldMeshHash.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace napa::voxel::testing
{
	namespace
	{
		[[nodiscard]] napa::voxel::ValidationResult PrepareAndCommitCpuMeshBatch(
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pending,
			napa::voxel::VisibleMeshSet& visible)
		{
			std::unique_ptr<napa::voxel::PreparedCpuMeshPublication> publication;
			const napa::voxel::ValidationResult result =
				napa::voxel::PrepareCpuMeshBatchPublication(pending, visible, publication);
			if (result.Succeeded())
			{
				napa::voxel::CommitCpuMeshBatchPublication(publication, visible);
			}
			return result;
		}

		inline constexpr std::array EightChunkDomain{
			napa::voxel::ChunkCoord{ -1, -1, -1 },
			napa::voxel::ChunkCoord{ 0, -1, -1 },
			napa::voxel::ChunkCoord{ -1, 0, -1 },
			napa::voxel::ChunkCoord{ 0, 0, -1 },
			napa::voxel::ChunkCoord{ -1, -1, 0 },
			napa::voxel::ChunkCoord{ 0, -1, 0 },
			napa::voxel::ChunkCoord{ -1, 0, 0 },
			napa::voxel::ChunkCoord{ 0, 0, 0 },
		};

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

		[[nodiscard]] napa::voxel::PrimitiveDesc MakeBox(
			std::uint64_t stableId,
			napa::voxel::Double3 center,
			napa::voxel::Double3 halfExtents) noexcept
		{
			using namespace napa::voxel;

			return {
				.m_StableId = { stableId },
				.m_Material = VoxelMaterial::Stone,
				.m_Shape = PrimitiveShape::AxisAlignedBox,
				.m_Parameters = {
					.m_AxisAlignedBox = {
						.m_Center = center,
						.m_HalfExtents = halfExtents,
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

			if (records.size() != EightChunkDomain.size())
			{
				return false;
			}
			for (std::size_t index = 0;
				index < records.size();
				++index)
			{
				if (records[index].m_Chunk != EightChunkDomain[index])
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
				for (const napa::voxel::BoundaryContourRecord& contour :
					record.m_BoundaryContours)
				{
					if (!contour.m_Segments.empty() ||
						contour.m_SkippedZeroLengthSegmentCount != 0)
					{
						return false;
					}
				}
			}
			return true;
		}

		[[nodiscard]] bool TouchesSharedPlanesAndEdge(
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

		[[nodiscard]] const napa::voxel::ChunkMeshRecord*
			FindChunkMeshRecord(
				std::span<const napa::voxel::ChunkMeshRecord> records,
				napa::voxel::ChunkCoord chunk) noexcept
		{
			for (const napa::voxel::ChunkMeshRecord& record : records)
			{
				if (record.m_Chunk == chunk)
				{
					return &record;
				}
			}
			return nullptr;
		}

		[[nodiscard]] const napa::voxel::BoundaryContourRecord*
			FindBoundaryContour(
				std::span<const napa::voxel::ChunkMeshRecord> records,
				napa::voxel::ChunkCoord chunk,
				napa::voxel::ChunkBoundaryFace face) noexcept
		{
			const napa::voxel::ChunkMeshRecord* const record =
				FindChunkMeshRecord(records, chunk);
			return record
				? &record->m_BoundaryContours[
					napa::voxel::GetChunkBoundaryFaceIndex(face)]
				: nullptr;
		}

		[[nodiscard]] bool ContainsBoundaryEndpoint(
			const napa::voxel::BoundaryContourRecord& contour,
			napa::voxel::QuantizedBoundaryContourPosition position)
			noexcept
		{
			for (const napa::voxel::BoundaryContourSegment& segment :
				contour.m_Segments)
			{
				if (segment.m_EndpointA.m_Position == position ||
					segment.m_EndpointB.m_Position == position)
				{
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool ContainsBoundarySegment(
			const napa::voxel::BoundaryContourRecord& contour,
			napa::voxel::QuantizedBoundaryContourPosition endpointA,
			napa::voxel::QuantizedBoundaryContourPosition endpointB)
			noexcept
		{
			for (const napa::voxel::BoundaryContourSegment& segment :
				contour.m_Segments)
			{
				if (segment.m_EndpointA.m_Position == endpointA &&
					segment.m_EndpointB.m_Position == endpointB)
				{
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool HaveIdenticalBoundaryContours(
			std::span<const napa::voxel::ChunkMeshRecord> lhs,
			std::span<const napa::voxel::ChunkMeshRecord> rhs) noexcept
		{
			if (lhs.size() != rhs.size())
			{
				return false;
			}
			for (std::size_t index = 0; index < lhs.size(); ++index)
			{
				if (lhs[index].m_Chunk != rhs[index].m_Chunk ||
					lhs[index].m_BoundaryContours != rhs[index].m_BoundaryContours)
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool OffsetFirstBoundaryNormal(
			std::vector<napa::voxel::ChunkMeshRecord>& records, std::int32_t stepCount)
		{
			using namespace napa::voxel;

			if (records.empty() || stepCount <= 0)
			{
				return false;
			}
			BoundaryContourRecord& contour = records[0].m_BoundaryContours[
				GetChunkBoundaryFaceIndex(ChunkBoundaryFace::PositiveX)];
			if (contour.m_Segments.empty())
			{
				return false;
			}

			QuantizedMeshNormal& normal = contour.m_Segments[0].m_EndpointA.m_Normal;
			std::int16_t* component = &normal.m_X;
			const auto magnitude = [](std::int16_t value) noexcept
				{
					const std::int32_t wide = value;
					return wide >= 0 ? wide : -wide;
				};
			if (magnitude(normal.m_Y) > magnitude(*component))
			{
				component = &normal.m_Y;
			}
			if (magnitude(normal.m_Z) > magnitude(*component))
			{
				component = &normal.m_Z;
			}

			const std::int32_t wide = *component;
			*component = static_cast<std::int16_t>(
				wide > 0 ? wide - stepCount : wide + stepCount);
			std::sort(contour.m_Segments.begin(), contour.m_Segments.end(),
				BoundaryContourSegmentLess{});
			return true;
		}

		void RunBoundaryContourContractTests(TestContext& context)
		{
			using namespace napa::voxel;

			constexpr std::array expectedBoundaryFaces{
				std::array{
					std::array<std::uint8_t, 3>{ 0, 2, 6 },
					std::array<std::uint8_t, 3>{ 0, 6, 4 },
				},
				std::array{
					std::array<std::uint8_t, 3>{ 1, 3, 7 },
					std::array<std::uint8_t, 3>{ 5, 1, 7 },
				},
				std::array{
					std::array<std::uint8_t, 3>{ 0, 4, 5 },
					std::array<std::uint8_t, 3>{ 0, 5, 1 },
				},
				std::array{
					std::array<std::uint8_t, 3>{ 3, 2, 7 },
					std::array<std::uint8_t, 3>{ 2, 6, 7 },
				},
				std::array{
					std::array<std::uint8_t, 3>{ 0, 1, 3 },
					std::array<std::uint8_t, 3>{ 0, 3, 2 },
				},
				std::array{
					std::array<std::uint8_t, 3>{ 6, 4, 7 },
					std::array<std::uint8_t, 3>{ 4, 5, 7 },
				},
			};
			context.Check(
				ReferenceBoundaryFaceTriangles ==
				expectedBoundaryFaces,
				"Boundary faces use the fixed Freudenthal diagonals");

			context.Check(
				AreBoundaryContourNormalsEquivalent(
					{ 32767, 0, 0 },
					{ 32766, 1, -1 }) &&
				!AreBoundaryContourNormalsEquivalent(
					{ 32767, 0, 0 },
					{ 32765, 0, 0 }),
				"Boundary normal comparison uses one SNORM16 step of tolerance");
		}

		void RunCompleteDomainTests(TestContext& context)
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
				0x9d97b7e13bde8d4cull &&
				meshing.m_BoundaryValidation ==
				BoundaryContourValidationResult{
					.m_ChunkRecordCount = 8,
					.m_ComparedFacePairCount = 12,
				},
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

		void RunBoundarySurfaceTests(TestContext& context)
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
						TouchesSharedPlanesAndEdge(record);
					if (!completeBoundaryCoverage)
					{
						break;
					}
				}
			}
			context.Check(
				completeBoundaryCoverage,
				"Face and edge surfaces mesh across positive halos and negative Chunks");
			if (!meshed)
			{
				return;
			}
			context.Check(
				meshing.m_BoundaryValidation
				.m_ChunkRecordCount == 8 &&
				meshing.m_BoundaryValidation
				.m_ComparedFacePairCount == 12 &&
				meshing.m_BoundaryValidation
				.m_ComparedSegmentCount > 0,
				"Boundary sphere contours match across all adjacent Chunks");
			context.Check(
				meshing.m_Validation ==
				WorldMeshValidationResult{
					.m_ValidationHash =
						0xb27f09eb3887560aull,
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
					meshing.m_Validation &&
					repeated.m_BoundaryValidation ==
					meshing.m_BoundaryValidation &&
					HaveIdenticalBoundaryContours(
						repeated.m_Chunks,
						meshing.m_Chunks);
			}
			context.Check(
				deterministic,
				"Repeated multi-Chunk meshing preserves complete boundary evidence");

			std::vector<ChunkMeshRecord> withinNormalTolerance = meshing.m_Chunks;
			BoundaryContourValidationResult withinToleranceValidation{};
			const bool withinToleranceInjected =
				OffsetFirstBoundaryNormal(withinNormalTolerance, 1);
			context.Check(
				withinToleranceInjected &&
				ValidateBoundaryContourSet(
					withinNormalTolerance,
					config,
					withinToleranceValidation).Succeeded() &&
				withinToleranceValidation ==
				meshing.m_BoundaryValidation,
				"Adjacent boundary contours accept one SNORM16 normal step end to end");

			const BoundaryContourValidationResult sentinel{
				.m_ChunkRecordCount = 11,
				.m_ComparedFacePairCount = 12,
				.m_ComparedSegmentCount = 13,
				.m_SkippedZeroLengthSegmentCount = 14,
			};
			BoundaryContourValidationResult unchanged = sentinel;
			std::vector<ChunkMeshRecord> outsideNormalTolerance = meshing.m_Chunks;
			const bool outsideToleranceInjected =
				OffsetFirstBoundaryNormal(outsideNormalTolerance, 2);
			context.Check(
				outsideToleranceInjected &&
				ValidateBoundaryContourSet(
					outsideNormalTolerance,
					config,
					unchanged).m_Error ==
				ValidationError::
				MismatchedBoundaryContour &&
				unchanged == sentinel,
				"Adjacent boundary contours reject two SNORM16 normal steps atomically");

			std::vector<ChunkMeshRecord> mismatched =
				meshing.m_Chunks;
			BoundaryContourRecord& mismatchedContour =
				mismatched[0].m_BoundaryContours[
					GetChunkBoundaryFaceIndex(
						ChunkBoundaryFace::PositiveX)];
			const bool injected =
				!mismatchedContour.m_Segments.empty();
			if (injected)
			{
				mismatchedContour.m_Segments.pop_back();
			}
			context.Check(
				injected &&
				ValidateBoundaryContourSet(
					mismatched,
					config,
					unchanged).m_Error ==
				ValidationError::
				MismatchedBoundaryContour &&
				unchanged == sentinel,
				"Boundary contour mismatch fails atomically");
			WorldMeshValidationResult renderMeshValidation{};
			context.Check(
				injected &&
				ValidateAndHashWorldMeshRecords(
					mismatched,
					config,
					renderMeshValidation).Succeeded() &&
				renderMeshValidation ==
				meshing.m_Validation,
				"Boundary evidence does not enter the render mesh hash");

			ChunkMeshRecord nonCanonical =
				meshing.m_Chunks[0];
			BoundaryContourRecord& nonCanonicalContour =
				nonCanonical.m_BoundaryContours[
					GetChunkBoundaryFaceIndex(
						ChunkBoundaryFace::PositiveX)];
			const bool hasSegment =
				!nonCanonicalContour.m_Segments.empty();
			if (hasSegment)
			{
				std::swap(
					nonCanonicalContour.m_Segments[0]
					.m_EndpointA,
					nonCanonicalContour.m_Segments[0]
					.m_EndpointB);
			}
			context.Check(
				hasSegment &&
				ValidateChunkBoundaryContourSet(
					nonCanonical.m_BoundaryContours,
					nonCanonical.m_Chunk,
					config).m_Error ==
				ValidationError::
				InvalidBoundaryContour,
				"Boundary contour validation rejects non-canonical endpoints");
		}

		void RunExactIsoCornerTests(TestContext& context)
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config =
				MakeEightChunkConfig();
			const std::array primitives{
				MakeBox(
					1,
					{ 2.0, 2.0, 2.0 },
					{ 2.0, 2.0, 2.0 }),
			};
			std::unique_ptr<VoxelWorld> world;
			const bool generated = GenerateWorld(
				config,
				primitives,
				world);
			VoxelSample cornerSample{};
			VoxelSample faceSampleA{};
			VoxelSample faceSampleB{};
			VoxelSample faceSampleC{};
			ReferenceWorldMeshingResult meshing{};
			const bool meshed =
				generated &&
				world->ReadCurrentSample(
					{},
					cornerSample).Succeeded() &&
				world->ReadCurrentSample(
					{ 0, 1, 1 },
					faceSampleA).Succeeded() &&
				world->ReadCurrentSample(
					{ 0, 2, 1 },
					faceSampleB).Succeeded() &&
				world->ReadCurrentSample(
					{ 0, 1, 2 },
					faceSampleC).Succeeded() &&
				ReferenceMesher(*world).MeshWorld(
					meshing).Succeeded();

			struct ExpectedCornerFace
			{
				ChunkCoord m_Chunk{};
				ChunkBoundaryFace m_Face =
					ChunkBoundaryFace::NegativeX;
			};
			// The exact-iso box occupies the positive octant. Its corner
			// contours lie on the two exterior quadrants of each shared
			// plane; the interior quadrant is entirely solid.
			constexpr std::array expectedCornerFaces{
				ExpectedCornerFace{
					{ -1, -1, 0 },
					ChunkBoundaryFace::PositiveX,
				},
				ExpectedCornerFace{
					{ 0, -1, 0 },
					ChunkBoundaryFace::NegativeX,
				},
				ExpectedCornerFace{
					{ -1, 0, -1 },
					ChunkBoundaryFace::PositiveX,
				},
				ExpectedCornerFace{
					{ 0, 0, -1 },
					ChunkBoundaryFace::NegativeX,
				},
				ExpectedCornerFace{
					{ -1, -1, 0 },
					ChunkBoundaryFace::PositiveY,
				},
				ExpectedCornerFace{
					{ -1, 0, 0 },
					ChunkBoundaryFace::NegativeY,
				},
				ExpectedCornerFace{
					{ 0, -1, -1 },
					ChunkBoundaryFace::PositiveY,
				},
				ExpectedCornerFace{
					{ 0, 0, -1 },
					ChunkBoundaryFace::NegativeY,
				},
				ExpectedCornerFace{
					{ -1, 0, -1 },
					ChunkBoundaryFace::PositiveZ,
				},
				ExpectedCornerFace{
					{ -1, 0, 0 },
					ChunkBoundaryFace::NegativeZ,
				},
				ExpectedCornerFace{
					{ 0, -1, -1 },
					ChunkBoundaryFace::PositiveZ,
				},
				ExpectedCornerFace{
					{ 0, -1, 0 },
					ChunkBoundaryFace::NegativeZ,
				},
			};
			constexpr QuantizedBoundaryContourPosition sharedCorner{};
			constexpr QuantizedBoundaryContourPosition exactEdgeEnd{
				0,
				0,
				BoundaryContourPositionScale,
			};
			std::uint32_t cornerFaceCount = 0;
			std::uint32_t exactEdgeFaceCount = 0;
			if (meshed)
			{
				for (const ExpectedCornerFace expected :
				expectedCornerFaces)
				{
					const BoundaryContourRecord* const contour =
						FindBoundaryContour(
							meshing.m_Chunks,
							expected.m_Chunk,
							expected.m_Face);
					if (contour &&
						ContainsBoundaryEndpoint(
							*contour,
							sharedCorner))
					{
						++cornerFaceCount;
					}
					if (contour &&
						ContainsBoundarySegment(
							*contour,
							sharedCorner,
							exactEdgeEnd))
					{
						++exactEdgeFaceCount;
					}
				}
			}
			context.Check(
				meshed &&
				cornerSample.m_Density == IsoValue &&
				cornerSample.m_Material ==
				VoxelMaterial::Stone &&
				cornerFaceCount ==
				expectedCornerFaces.size(),
				"Box surface contours meet at the shared Chunk corner");
			context.Check(
				meshed &&
				exactEdgeFaceCount == 4,
				"Exact-iso face edges produce matching nonzero contours");
			context.Check(
				meshed &&
				faceSampleA.m_Density == IsoValue &&
				faceSampleB.m_Density == IsoValue &&
				faceSampleC.m_Density == IsoValue &&
				meshing.m_BoundaryValidation
				.m_SkippedZeroLengthSegmentCount > 0,
				"Exact-iso face vertices discard zero-length contours before normal validation");

			const BoundaryContourRecord* const fullIsoFace =
				meshed
				? FindBoundaryContour(
					meshing.m_Chunks,
					{},
					ChunkBoundaryFace::NegativeX)
				: nullptr;
			bool hasInteriorSegment = false;
			if (fullIsoFace)
			{
				const std::int64_t maximumInterior =
					4 * BoundaryContourPositionScale;
				for (const BoundaryContourSegment& segment :
					fullIsoFace->m_Segments)
				{
					const auto isInterior =
						[maximumInterior](
							QuantizedBoundaryContourPosition
							position)
						{
							return
								position.m_Y > 0 &&
								position.m_Y <
								maximumInterior &&
								position.m_Z > 0 &&
								position.m_Z <
								maximumInterior;
						};
					if (isInterior(
						segment.m_EndpointA.m_Position) &&
						isInterior(
							segment.m_EndpointB.m_Position))
					{
						hasInteriorSegment = true;
						break;
					}
				}
			}
			context.Check(
				fullIsoFace != nullptr &&
				!hasInteriorSegment,
				"Fully exact-iso boundary triangles emit no interior contour");
		}

		void RunZeroGradientBoundaryFallbackTests(TestContext& context)
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config{
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = {},
					.m_MaxExclusive = { 16, 8, 8 },
				},
			};
			std::unique_ptr<VoxelWorld> world;
			bool initialized = VoxelWorld::Create(config, world).Succeeded() && world;
			for (std::int32_t z = 0; z <= 8 && initialized; ++z)
			{
				for (std::int32_t y = 0; y <= 8 && initialized; y += 2)
				{
					for (std::int32_t x = 0; x <= 16; ++x)
					{
						bool changed = false;
						initialized = world->WriteCurrentSample({ x, y, z }, {
							.m_Density = 255,
							.m_Material = VoxelMaterial::Stone,
							}, changed).Succeeded() && changed;
						if (!initialized)
						{
							break;
						}
					}
				}
			}

			ReferenceWorldMeshingResult first{};
			ReferenceWorldMeshingResult repeated{};
			const bool meshed = initialized &&
				ReferenceMesher(*world).MeshWorld(first).Succeeded() &&
				ReferenceMesher(*world).MeshWorld(repeated).Succeeded();
			context.Check(meshed && first.m_Chunks.size() == 2 &&
				first.m_Validation == repeated.m_Validation &&
				first.m_BoundaryValidation == repeated.m_BoundaryValidation &&
				first.m_BoundaryValidation.m_ComparedFacePairCount == 1 &&
				first.m_BoundaryValidation.m_ComparedSegmentCount > 0,
				"Zero-gradient topology fallback preserves adjacent Chunk boundary contours");
		}

		void RunCpuMeshBatchTests(TestContext& context)
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config = MakeEightChunkConfig();
			const std::array primitives{
				MakeSphere(1, {}, 3.0),
			};
			std::unique_ptr<VoxelWorld> world;
			const bool generated = GenerateWorld(config, primitives, world);

			VisibleMeshSet visible;
			const WorldMeshValidationResult hashSentinel{
				.m_ValidationHash = 0x123456789abcdef0ull,
				.m_ChunkCount = 1,
				.m_VertexCount = 2,
				.m_SectionCount = 3,
				.m_IndexCount = 4,
				.m_TriangleCount = 5,
				.m_SkippedDegenerateTriangleCount = 6,
			};
			WorldMeshValidationResult unchangedHash = hashSentinel;
			context.Check(
				generated &&
				!visible.HasPublishedMeshes() &&
				visible.GetVisibleWorldRevision() == 0 &&
				visible.GetChunks().empty() &&
				ComputeVisibleWorldMeshHash(visible, unchangedHash).m_Error ==
				ValidationError::VisibleMeshSetUninitialized &&
				unchangedHash == hashSentinel,
				"An unpublished Visible Mesh Set has revision zero and no readable hash");

			CpuMeshBatch fullBatch{};
			const bool built = generated &&
				BuildCpuMeshBatch(*world, world->GetWorldVoxelRevision(),
					EightChunkDomain, fullBatch).Succeeded();
			bool canonicalCandidates = built &&
				fullBatch.m_BaseWorldVoxelRevision == 0 &&
				fullBatch.m_TargetWorldVoxelRevision == 1 &&
				fullBatch.m_RequestedChunks.size() == EightChunkDomain.size() &&
				fullBatch.m_Candidates.size() == EightChunkDomain.size();
			for (std::size_t index = 0;
				index < fullBatch.m_Candidates.size() && canonicalCandidates;
				++index)
			{
				canonicalCandidates =
					fullBatch.m_RequestedChunks[index] == EightChunkDomain[index] &&
					fullBatch.m_Candidates[index].m_Chunk == EightChunkDomain[index] &&
					fullBatch.m_Candidates[index].m_SourceWorldVoxelRevision == 1;
			}
			context.Check(
				canonicalCandidates,
				"CPU mesh batch build preserves the requested canonical Candidate Set");
			if (!built)
			{
				return;
			}

			CpuMeshBatch unchangedBuild = fullBatch;
			const ChunkMeshRecord* const unchangedBuildData =
				unchangedBuild.m_Candidates.data();
			context.Check(
				BuildCpuMeshBatch(
					*world, 0, EightChunkDomain, unchangedBuild).m_Error ==
				ValidationError::MismatchedCpuMeshTargetRevision &&
				unchangedBuild.m_TargetWorldVoxelRevision == 1 &&
				unchangedBuild.m_Candidates.data() == unchangedBuildData,
				"CPU mesh batch build rejects a mismatched Target revision atomically");

			std::unique_ptr<PendingCpuMeshBatch> pending;
			const bool validated =
				ValidateCpuMeshBatch(fullBatch, visible, pending).Succeeded() &&
				pending != nullptr;
			PendingCpuMeshBatch* const validatedPending = pending.get();
			context.Check(
				validated &&
				pending->GetBaseWorldVoxelRevision() == 0 &&
				pending->GetTargetWorldVoxelRevision() == 1 &&
				pending->GetCandidateChunkCount() == EightChunkDomain.size() &&
				pending->GetChunks().size() == EightChunkDomain.size() &&
				pending->GetReplacementChunks().size() == EightChunkDomain.size() &&
				!visible.HasPublishedMeshes() &&
				visible.GetVisibleWorldRevision() == 0,
				"Validated Pending CPU meshes leave Visible state unchanged");
			if (!validated)
			{
				return;
			}

			CpuMeshBatch missingCandidate = fullBatch;
			missingCandidate.m_Candidates.pop_back();
			context.Check(
				ValidateCpuMeshBatch(missingCandidate, visible, pending).m_Error ==
				ValidationError::MismatchedCpuMeshCandidateSet &&
				pending.get() == validatedPending &&
				!visible.HasPublishedMeshes(),
				"Missing CPU mesh Candidates fail without replacing Pending or Visible state");

			CpuMeshBatch extraCandidate = fullBatch;
			extraCandidate.m_Candidates.push_back(fullBatch.m_Candidates.back());
			context.Check(
				ValidateCpuMeshBatch(extraCandidate, visible, pending).m_Error ==
				ValidationError::MismatchedCpuMeshCandidateSet &&
				pending.get() == validatedPending &&
				!visible.HasPublishedMeshes(),
				"Extra CPU mesh Candidates fail without replacing Pending or Visible state");

			CpuMeshBatch sourceRevisionMismatch = fullBatch;
			sourceRevisionMismatch.m_Candidates[0].m_SourceWorldVoxelRevision = 2;
			context.Check(
				ValidateCpuMeshBatch(sourceRevisionMismatch, visible, pending).m_Error ==
				ValidationError::MismatchedCpuMeshSourceRevision &&
				pending.get() == validatedPending &&
				!visible.HasPublishedMeshes(),
				"CPU mesh Candidate source revisions must match the Batch target");

			constexpr std::array incompleteInitialChunks{
				ChunkCoord{ -1, -1, -1 },
			};
			CpuMeshBatch incompleteInitialBatch{};
			const bool incompleteInitialBuilt =
				BuildCpuMeshBatch(
					*world, 1, incompleteInitialChunks, incompleteInitialBatch).Succeeded();
			context.Check(
				incompleteInitialBuilt &&
				ValidateCpuMeshBatch(
					incompleteInitialBatch, visible, pending).m_Error ==
				ValidationError::InvalidWorldMeshRecordSet &&
				pending.get() == validatedPending &&
				!visible.HasPublishedMeshes(),
				"Initial CPU mesh publication requires the complete Cell-owner Chunk Domain");

			CpuMeshBatch invalidCandidate = fullBatch;
			invalidCandidate.m_Candidates[0].m_Validation.m_ValidationHash ^= 1;
			context.Check(
				ValidateCpuMeshBatch(invalidCandidate, visible, pending).m_Error ==
				ValidationError::MismatchedChunkMeshValidation &&
				pending.get() == validatedPending &&
				!visible.HasPublishedMeshes(),
				"One invalid CPU mesh Candidate prevents partial publication");

			CpuMeshBatch candidateSeamFailure = fullBatch;
			BoundaryContourRecord& candidateContour =
				candidateSeamFailure.m_Candidates[0].m_BoundaryContours[
					GetChunkBoundaryFaceIndex(ChunkBoundaryFace::PositiveX)];
			const bool candidateSeamInjected = !candidateContour.m_Segments.empty();
			if (candidateSeamInjected)
			{
				candidateContour.m_Segments.pop_back();
			}
			context.Check(
				candidateSeamInjected &&
				ValidateCpuMeshBatch(candidateSeamFailure, visible, pending).m_Error ==
				ValidationError::MismatchedBoundaryContour &&
				pending.get() == validatedPending &&
				!visible.HasPublishedMeshes(),
				"Candidate-to-Candidate seam failure prevents partial publication");

			std::unique_ptr<PendingCpuMeshBatch> competingInitialPending;
			const bool competingInitialValidated =
				ValidateCpuMeshBatch(fullBatch, visible, competingInitialPending).Succeeded();
			const WorldMeshValidationResult initialValidation =
				pending->GetWorldMeshValidation();
			std::unique_ptr<PreparedCpuMeshPublication> initialPublication;
			const bool initialPrepared = PrepareCpuMeshBatchPublication(
				pending, visible, initialPublication).Succeeded();
			context.Check(initialPrepared && !pending && initialPublication &&
				!visible.HasPublishedMeshes() && visible.GetVisibleWorldRevision() == 0,
				"CPU mesh publication preparation leaves Visible state unchanged");
			if (initialPrepared)
			{
				CommitCpuMeshBatchPublication(initialPublication, visible);
			}
			context.Check(
				competingInitialValidated &&
				initialPrepared && !initialPublication &&
				visible.HasPublishedMeshes() &&
				visible.GetVisibleWorldRevision() == 1 &&
				visible.GetChunks().size() == EightChunkDomain.size() &&
				visible.GetWorldMeshValidation() == initialValidation,
				"Publishing replaces the complete Visible Mesh Set at one Safe Point");

			WorldMeshValidationResult visibleValidation{};
			context.Check(
				ComputeVisibleWorldMeshHash(visible, visibleValidation).Succeeded() &&
				visibleValidation == initialValidation &&
				visibleValidation.m_ValidationHash == 0xb27f09eb3887560aull,
				"Visible World mesh hashing reads only the published Mesh Set");

			std::unique_ptr<PendingCpuMeshBatch> sameRevisionPending;
			context.Check(
				ValidateCpuMeshBatch(fullBatch, visible, sameRevisionPending).m_Error ==
				ValidationError::StaleCpuMeshBatch &&
				sameRevisionPending == nullptr &&
				visible.GetVisibleWorldRevision() == 1 &&
				visible.GetWorldMeshValidation() == initialValidation,
				"A published Visible revision cannot create another same-revision Pending batch");

			const PendingCpuMeshBatch* const staleInitialPending =
				competingInitialPending.get();
			std::unique_ptr<PreparedCpuMeshPublication> staleInitialPublication;
			context.Check(
				PrepareCpuMeshBatchPublication(competingInitialPending, visible,
					staleInitialPublication).m_Error ==
				ValidationError::StaleCpuMeshBatch &&
				competingInitialPending.get() == staleInitialPending &&
				!staleInitialPublication &&
				visible.GetVisibleWorldRevision() == 1 &&
				visible.GetWorldMeshValidation() == initialValidation,
				"A Pending batch cannot publish over a different Visible base");

			CpuMeshBatch mismatchedConfig = fullBatch;
			mismatchedConfig.m_Config.m_VoxelSize = 2.0f;
			std::unique_ptr<PendingCpuMeshBatch> mismatchedConfigPending;
			context.Check(
				ValidateCpuMeshBatch(
					mismatchedConfig, visible, mismatchedConfigPending).m_Error ==
				ValidationError::MismatchedCpuMeshConfig &&
				mismatchedConfigPending == nullptr &&
				visible.GetVisibleWorldRevision() == 1,
				"CPU mesh batches cannot merge into a differently configured Visible Set");

			bool changed = false;
			VoxelSample materialEdit{};
			const bool materialRead =
				world->ReadCurrentSample({ -1, -1, -1 }, materialEdit).Succeeded() &&
				materialEdit.m_Density >= IsoValue &&
				materialEdit.m_Material == VoxelMaterial::Stone;
			const VoxelSample materialBefore = materialEdit;
			if (materialRead)
			{
				materialEdit.m_Material = VoxelMaterial::Soil;
			}
			const bool edited =
				materialRead &&
				world->WriteCurrentSample({ -1, -1, -1 }, materialEdit, changed).Succeeded() &&
				changed &&
				world->GetWorldVoxelRevision() == 2;
			constexpr std::array partialChunks{
				ChunkCoord{ -1, -1, -1 },
			};
			VoxelMutationResult materialMutation{
				.m_BaseWorldVoxelRevision = 1,
				.m_TargetWorldVoxelRevision = 2,
				.m_SampleChanges = {
					{
						.m_Coordinate = { -1, -1, -1 },
						.m_Before = materialBefore,
						.m_After = materialEdit,
					},
				},
			};
			const bool materialDirtyDerived = edited &&
				DeriveVoxelMutationDirtyChunks(config, materialMutation.m_SampleChanges,
					materialMutation.m_DataDirtyChunks,
					materialMutation.m_MeshDirtyChunks).Succeeded() &&
				materialMutation.m_MeshDirtyChunks.size() == 1 &&
				materialMutation.m_MeshDirtyChunks[0] == partialChunks[0];
			CpuMeshBatch partialBatch{};
			const bool partialBuilt = materialDirtyDerived &&
				BuildCpuMeshBatch(*world, materialMutation, partialBatch).Succeeded();
			if (!partialBuilt)
			{
				context.Check(false, "A local edit builds an explicit partial CPU mesh batch");
				return;
			}

			CpuMeshBatch currentNeighborFailure = partialBatch;
			BoundaryContourRecord& currentNeighborContour =
				currentNeighborFailure.m_Candidates[0].m_BoundaryContours[
					GetChunkBoundaryFaceIndex(ChunkBoundaryFace::PositiveX)];
			const bool currentNeighborFaultInjected =
				!currentNeighborContour.m_Segments.empty();
			if (currentNeighborFaultInjected)
			{
				currentNeighborContour.m_Segments.pop_back();
			}
			std::unique_ptr<PendingCpuMeshBatch> partialPending;
			context.Check(
				currentNeighborFaultInjected &&
				ValidateCpuMeshBatch(
					currentNeighborFailure, visible, partialPending).m_Error ==
				ValidationError::MismatchedBoundaryContour &&
				partialPending == nullptr &&
				visible.GetVisibleWorldRevision() == 1 &&
				visible.GetWorldMeshValidation() == initialValidation,
				"Candidate-to-current Neighbor seam failure leaves Visible state unchanged");

			const bool partialValidated =
				ValidateCpuMeshBatch(partialBatch, visible, partialPending).Succeeded() &&
				partialPending != nullptr;
			std::unique_ptr<PendingCpuMeshBatch> competingPartialPending;
			const bool competingPartialValidated =
				ValidateCpuMeshBatch(
					partialBatch, visible, competingPartialPending).Succeeded();
			context.Check(
				partialValidated &&
				competingPartialValidated &&
				partialPending->GetBaseWorldVoxelRevision() == 1 &&
				partialPending->GetTargetWorldVoxelRevision() == 2 &&
				partialPending->GetCandidateChunkCount() == 1 &&
				partialPending->GetReplacementChunks().size() == 1 &&
				partialPending->GetReplacementChunks()[0].m_Chunk == partialChunks[0] &&
				visible.GetVisibleWorldRevision() == 1 &&
				visible.GetWorldMeshValidation() == initialValidation,
				"Partial Pending validation compares retained current Neighbors without publishing");

			const bool partialPublished =
				partialValidated &&
				PrepareAndCommitCpuMeshBatch(partialPending, visible).Succeeded();
			const ChunkMeshRecord* const rebuiltRecord =
				FindChunkMeshRecord(visible.GetChunks(), { -1, -1, -1 });
			const ChunkMeshRecord* const retainedRecord =
				FindChunkMeshRecord(visible.GetChunks(), {});
			WorldMeshValidationResult editedValidation{};
			context.Check(
				partialPublished &&
				visible.GetVisibleWorldRevision() == 2 &&
				rebuiltRecord != nullptr &&
				rebuiltRecord->m_SourceWorldVoxelRevision == 2 &&
				retainedRecord != nullptr &&
				retainedRecord->m_SourceWorldVoxelRevision == 1 &&
				ComputeVisibleWorldMeshHash(visible, editedValidation).Succeeded() &&
				editedValidation.m_ValidationHash !=
				initialValidation.m_ValidationHash,
				"Partial publication advances Visible revision while retaining valid older records");

			const PendingCpuMeshBatch* const stalePartialPending =
				competingPartialPending.get();
			std::unique_ptr<PreparedCpuMeshPublication> stalePartialPublication;
			context.Check(
				PrepareCpuMeshBatchPublication(competingPartialPending, visible,
					stalePartialPublication).m_Error ==
				ValidationError::StaleCpuMeshBatch &&
				competingPartialPending.get() == stalePartialPending &&
				!stalePartialPublication &&
				visible.GetVisibleWorldRevision() == 2 &&
				visible.GetWorldMeshValidation() == editedValidation,
				"Concurrent Pending publication is rejected atomically after the base changes");

			VoxelSample damageOnlyEdit{};
			bool damageOnlyChanged = false;
			const bool damageOnlyRead =
				world->ReadCurrentSample({ -1, -1, -1 }, damageOnlyEdit).Succeeded() &&
				damageOnlyEdit.m_Density >= IsoValue &&
				damageOnlyEdit.m_Material == VoxelMaterial::Soil;
			if (damageOnlyRead)
			{
				damageOnlyEdit.m_Damage = 1;
			}
			const bool damageOnlyWritten =
				damageOnlyRead &&
				world->WriteCurrentSample(
					{ -1, -1, -1 }, damageOnlyEdit, damageOnlyChanged).Succeeded() &&
				damageOnlyChanged &&
				world->GetWorldVoxelRevision() == 3;
			CpuMeshBatch damageOnlyBatch{};
			std::unique_ptr<PendingCpuMeshBatch> damageOnlyPending;
			const bool emptyBatchBuilt = damageOnlyWritten &&
				BuildCpuMeshBatch(*world, 3, std::span<const ChunkCoord>{},
					damageOnlyBatch).Succeeded();
			if (emptyBatchBuilt)
			{
				damageOnlyBatch.m_BaseWorldVoxelRevision = visible.GetVisibleWorldRevision();
			}
			const bool damageOnlyValidated = emptyBatchBuilt &&
				ValidateCpuMeshBatch(damageOnlyBatch, visible, damageOnlyPending).m_Error ==
				ValidationError::InvalidCpuMeshCandidateSet &&
				damageOnlyPending == nullptr;
			WorldMeshValidationResult damageOnlyValidation{};
			context.Check(
				damageOnlyValidated &&
				visible.GetVisibleWorldRevision() == 2 &&
				ComputeVisibleWorldMeshHash(
					visible, damageOnlyValidation).Succeeded() &&
				damageOnlyValidation == editedValidation,
				"An empty Mesh Batch cannot advance a published Visible revision");
		}

		void RunEmptyCpuMeshBatchTests(TestContext& context)
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config = MakeEightChunkConfig();
			std::unique_ptr<VoxelWorld> world;
			VisibleMeshSet visible;
			CpuMeshBatch batch{};
			std::unique_ptr<PendingCpuMeshBatch> pending;
			WorldMeshValidationResult validation{};
			const bool published =
				GenerateWorld(config, {}, world) &&
				BuildCpuMeshBatch(*world, 1, EightChunkDomain, batch).Succeeded() &&
				ValidateCpuMeshBatch(batch, visible, pending).Succeeded() &&
				PrepareAndCommitCpuMeshBatch(pending, visible).Succeeded() &&
				ComputeVisibleWorldMeshHash(visible, validation).Succeeded();
			bool allChunksEmpty = published && visible.GetChunks().size() == EightChunkDomain.size();
			for (const ChunkMeshRecord& record : visible.GetChunks())
			{
				allChunksEmpty = allChunksEmpty &&
					record.m_Mesh.m_Vertices.empty() &&
					record.m_Mesh.m_Sections.empty();
			}
			context.Check(
				allChunksEmpty &&
				validation.m_ChunkCount == EightChunkDomain.size() &&
				validation.m_ValidationHash == 0x9d97b7e13bde8d4cull,
				"Visible World mesh hashing includes every Empty Mesh Chunk");
		}

		void RunGuardAllocationTests(TestContext& context)
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
		TestContext& context) noexcept
	{
		RunBoundaryContourContractTests(context);
		RunCompleteDomainTests(context);
		RunBoundarySurfaceTests(context);
		RunExactIsoCornerTests(context);
		RunZeroGradientBoundaryFallbackTests(context);
		RunCpuMeshBatchTests(context);
		RunEmptyCpuMeshBatchTests(context);
		RunGuardAllocationTests(context);
	}
}
