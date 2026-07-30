#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Meshing/BoundaryContour.h"
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
				for (const napa::voxel::BoundaryContourRecord& contour :
					record.m_BoundaryContours)
				{
					if (!contour.m_Segments.empty() ||
						contour
							.m_SkippedZeroLengthSegmentCount != 0)
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

		void RunBoundaryContourContractTests(SelfTestContext& context)
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
						0x572bf6dcaaab0aa0ull &&
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

			const BoundaryContourValidationResult sentinel{
				.m_ChunkRecordCount = 11,
				.m_ComparedFacePairCount = 12,
				.m_ComparedSegmentCount = 13,
				.m_SkippedZeroLengthSegmentCount = 14,
			};
			BoundaryContourValidationResult unchanged = sentinel;
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

		void RunExactIsoCornerTests(SelfTestContext& context)
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
		RunBoundaryContourContractTests(context);
		RunCompleteDomainTests(context);
		RunBoundarySurfaceTests(context);
		RunExactIsoCornerTests(context);
		RunGuardAllocationTests(context);
	}
}
