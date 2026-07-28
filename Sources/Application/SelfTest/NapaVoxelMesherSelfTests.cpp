#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/Meshing/MeshData.h"
#include "NapaVoxelCore/Meshing/MeshValidation.h"
#include "NapaVoxelCore/Meshing/ReferenceMesher.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>

namespace gglab
{
	namespace
	{
		[[nodiscard]] napa::voxel::VoxelWorldConfig
			MakeMeshValidationConfig() noexcept
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

		[[nodiscard]] napa::voxel::MeshData
			MakeSyntheticTriangleMesh()
		{
			using namespace napa::voxel;

			return {
				.m_Vertices = {
					{
						.m_Position = { 1.0f, 1.0f, 1.0f },
						.m_Normal = { 0.0f, 0.0f, 1.0f },
					},
					{
						.m_Position = { 2.0f, 1.0f, 1.0f },
						.m_Normal = { 0.0f, 0.0f, 1.0f },
					},
					{
						.m_Position = { 1.0f, 2.0f, 1.0f },
						.m_Normal = { 0.0f, 0.0f, 1.0f },
					},
				},
				.m_Sections = {
					{
						.m_Material = VoxelMaterial::Soil,
						.m_Indices = { 0, 1, 2 },
					},
				},
				.m_Bounds = {
					.m_Min = { 1.0f, 1.0f, 1.0f },
					.m_Max = { 2.0f, 2.0f, 1.0f },
				},
			};
		}

		[[nodiscard]] napa::voxel::MeshData
			MakeSyntheticMultiMaterialMesh()
		{
			using namespace napa::voxel;

			return {
				.m_Vertices = {
					{
						.m_Position = { 1.0f, 1.0f, 1.0f },
						.m_Normal = { 0.0f, 0.0f, 1.0f },
					},
					{
						.m_Position = { 2.0f, 1.0f, 1.0f },
						.m_Normal = { 0.0f, 0.0f, 1.0f },
					},
					{
						.m_Position = { 2.0f, 2.0f, 1.0f },
						.m_Normal = { 0.0f, 0.0f, 1.0f },
					},
					{
						.m_Position = { 1.0f, 2.0f, 1.0f },
						.m_Normal = { 0.0f, 0.0f, 1.0f },
					},
				},
				.m_Sections = {
					{
						.m_Material = VoxelMaterial::Soil,
						.m_Indices = { 0, 1, 2 },
					},
					{
						.m_Material = VoxelMaterial::Stone,
						.m_Indices = { 0, 2, 3 },
					},
				},
				.m_Bounds = {
					.m_Min = { 1.0f, 1.0f, 1.0f },
					.m_Max = { 2.0f, 2.0f, 1.0f },
				},
			};
		}

		[[nodiscard]] bool NearlyEqual(
			double lhs,
			double rhs,
			double tolerance = 1.0e-6) noexcept
		{
			return std::abs(lhs - rhs) <= tolerance;
		}

		[[nodiscard]] std::array<
			napa::voxel::ReferenceEdgeEndpoint,
			8> MakeReferenceCubeCorners(
				std::uint8_t classification,
				std::uint8_t solidDensity = 192,
				std::uint8_t emptyDensity = 64) noexcept
		{
			using namespace napa::voxel;

			std::array<ReferenceEdgeEndpoint, 8> corners{};
			for (std::size_t cornerIndex = 0;
				cornerIndex < corners.size();
				++cornerIndex)
			{
				const CellCornerOffset offset =
					ReferenceCubeCornerOffsets[cornerIndex];
				corners[cornerIndex] = {
					.m_Coordinate = {
						static_cast<std::int32_t>(offset.m_X),
						static_cast<std::int32_t>(offset.m_Y),
						static_cast<std::int32_t>(offset.m_Z),
					},
					.m_Sample = {
						.m_Density = emptyDensity,
						.m_Material = VoxelMaterial::Empty,
						.m_Damage = 0,
					},
					.m_DensityGradient = { 1.0, 0.0, 0.0 },
				};
			}

			const std::array<std::uint8_t, 4>& tetrahedron =
				ReferenceFreudenthalTetrahedra[0];
			DensityGradient solidSum{};
			DensityGradient emptySum{};
			double solidCount = 0.0;
			double emptyCount = 0.0;
			for (std::size_t localIndex = 0;
				localIndex < tetrahedron.size();
				++localIndex)
			{
				const std::uint8_t cornerId =
					tetrahedron[localIndex];
				const bool solid =
					(classification &
						(static_cast<std::uint8_t>(1) <<
							localIndex)) != 0;
				ReferenceEdgeEndpoint& corner =
					corners[cornerId];
				corner.m_Sample = solid
					? VoxelSample{
						.m_Density = solidDensity,
						.m_Material = VoxelMaterial::Stone,
						.m_Damage = 0,
					}
					: VoxelSample{
						.m_Density = emptyDensity,
						.m_Material = VoxelMaterial::Empty,
						.m_Damage = 0,
					};

				DensityGradient& sum =
					solid ? solidSum : emptySum;
				sum.m_X += corner.m_Coordinate.m_X;
				sum.m_Y += corner.m_Coordinate.m_Y;
				sum.m_Z += corner.m_Coordinate.m_Z;
				if (solid)
				{
					solidCount += 1.0;
				}
				else
				{
					emptyCount += 1.0;
				}
			}

			if (solidCount > 0.0 && emptyCount > 0.0)
			{
				const DensityGradient densityGradient{
					solidSum.m_X / solidCount -
						emptySum.m_X / emptyCount,
					solidSum.m_Y / solidCount -
						emptySum.m_Y / emptyCount,
					solidSum.m_Z / solidCount -
						emptySum.m_Z / emptyCount,
				};
				for (const std::uint8_t cornerId : tetrahedron)
				{
					corners[cornerId].m_DensityGradient =
						densityGradient;
				}
			}
			return corners;
		}

		[[nodiscard]] std::uint8_t CountTetrahedronSolidBits(
			std::uint8_t classification) noexcept
		{
			std::uint8_t count = 0;
			for (std::uint8_t bit = 0; bit < 4; ++bit)
			{
				count += static_cast<std::uint8_t>(
					(classification >>
						bit) &
					static_cast<std::uint8_t>(1));
			}
			return count;
		}

		[[nodiscard]] bool HasOutwardNormalWinding(
			const napa::voxel::ReferenceTriangle& triangle) noexcept
		{
			using namespace napa::voxel;

			const Float3 a = triangle.m_Vertices[0].m_Position;
			const Float3 b = triangle.m_Vertices[1].m_Position;
			const Float3 c = triangle.m_Vertices[2].m_Position;
			const double abX =
				static_cast<double>(b.m_X) -
				static_cast<double>(a.m_X);
			const double abY =
				static_cast<double>(b.m_Y) -
				static_cast<double>(a.m_Y);
			const double abZ =
				static_cast<double>(b.m_Z) -
				static_cast<double>(a.m_Z);
			const double acX =
				static_cast<double>(c.m_X) -
				static_cast<double>(a.m_X);
			const double acY =
				static_cast<double>(c.m_Y) -
				static_cast<double>(a.m_Y);
			const double acZ =
				static_cast<double>(c.m_Z) -
				static_cast<double>(a.m_Z);
			const double crossX = abY * acZ - abZ * acY;
			const double crossY = abZ * acX - abX * acZ;
			const double crossZ = abX * acY - abY * acX;
			const double normalX =
				triangle.m_Vertices[0].m_Normal.m_X +
				triangle.m_Vertices[1].m_Normal.m_X +
				triangle.m_Vertices[2].m_Normal.m_X;
			const double normalY =
				triangle.m_Vertices[0].m_Normal.m_Y +
				triangle.m_Vertices[1].m_Normal.m_Y +
				triangle.m_Vertices[2].m_Normal.m_Y;
			const double normalZ =
				triangle.m_Vertices[0].m_Normal.m_Z +
				triangle.m_Vertices[1].m_Normal.m_Z +
				triangle.m_Vertices[2].m_Normal.m_Z;
			return
				crossX * normalX +
				crossY * normalY +
				crossZ * normalZ > 0.0;
		}

		[[nodiscard]] bool HasDirectionWinding(
			const napa::voxel::ReferenceTriangle& triangle,
			napa::voxel::DensityGradient direction) noexcept
		{
			const napa::voxel::Float3 a =
				triangle.m_Vertices[0].m_Position;
			const napa::voxel::Float3 b =
				triangle.m_Vertices[1].m_Position;
			const napa::voxel::Float3 c =
				triangle.m_Vertices[2].m_Position;
			const double abX =
				static_cast<double>(b.m_X) -
				static_cast<double>(a.m_X);
			const double abY =
				static_cast<double>(b.m_Y) -
				static_cast<double>(a.m_Y);
			const double abZ =
				static_cast<double>(b.m_Z) -
				static_cast<double>(a.m_Z);
			const double acX =
				static_cast<double>(c.m_X) -
				static_cast<double>(a.m_X);
			const double acY =
				static_cast<double>(c.m_Y) -
				static_cast<double>(a.m_Y);
			const double acZ =
				static_cast<double>(c.m_Z) -
				static_cast<double>(a.m_Z);
			const double crossX = abY * acZ - abZ * acY;
			const double crossY = abZ * acX - abX * acZ;
			const double crossZ = abX * acY - abY * acX;
			return
				crossX * direction.m_X +
				crossY * direction.m_Y +
				crossZ * direction.m_Z > 0.0;
		}

		[[nodiscard]] bool IsVertexOnCubeEdge(
			const napa::voxel::ReferenceEdgeVertex& vertex,
			const std::array<
				napa::voxel::ReferenceEdgeEndpoint,
				8>& cubeCorners,
			std::uint8_t firstCornerId,
			std::uint8_t secondCornerId) noexcept
		{
			const napa::voxel::SampleCoord first =
				cubeCorners[firstCornerId].m_Coordinate;
			const napa::voxel::SampleCoord second =
				cubeCorners[secondCornerId].m_Coordinate;
			return
				(vertex.m_EndpointA == first &&
					vertex.m_EndpointB == second) ||
				(vertex.m_EndpointA == second &&
					vertex.m_EndpointB == first);
		}

		[[nodiscard]] bool TriangleContainsCubeEdge(
			const napa::voxel::ReferenceTriangle& triangle,
			const std::array<
				napa::voxel::ReferenceEdgeEndpoint,
				8>& cubeCorners,
			std::uint8_t firstCornerId,
			std::uint8_t secondCornerId) noexcept
		{
			for (const napa::voxel::ReferenceEdgeVertex& vertex :
				triangle.m_Vertices)
			{
				if (IsVertexOnCubeEdge(
					vertex,
					cubeCorners,
					firstCornerId,
					secondCornerId))
				{
					return true;
				}
			}
			return false;
		}

		void RunMeshDataLayoutTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			context.Check(
				std::is_standard_layout_v<Float3> &&
					std::is_trivially_copyable_v<Float3> &&
					sizeof(Float3) == 12,
				"Float3 has a portable three-float data layout");
			context.Check(
				std::is_standard_layout_v<FloatAabb> &&
					std::is_trivially_copyable_v<FloatAabb> &&
					sizeof(FloatAabb) == 24,
				"FloatAabb has a portable two-corner data layout");
			context.Check(
				std::is_standard_layout_v<MeshVertex> &&
					std::is_trivially_copyable_v<MeshVertex> &&
					sizeof(MeshVertex) == 24,
				"MeshVertex has a portable position-normal data layout");
		}

		void RunMeshQuantizationTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			MeshQuantizationContext unprepared;
			QuantizedMeshPosition position{
				.m_X = 7,
				.m_Y = 8,
				.m_Z = 9,
			};
			context.Check(
				QuantizeMeshPosition(
					{},
					unprepared,
					position).m_Error ==
					ValidationError::UnpreparedMeshQuantizationContext &&
					position == QuantizedMeshPosition{ 7, 8, 9 },
				"Mesh position quantization rejects unprepared contexts atomically");

			MeshQuantizationContext contextAtOrigin;
			const VoxelWorldConfig config =
				MakeMeshValidationConfig();
			context.Check(
				PrepareMeshQuantizationContext(
					config,
					{},
					contextAtOrigin).Succeeded(),
				"Mesh position quantization prepares a validated context");

			MeshQuantizationContext rejectedContext =
				contextAtOrigin;
			QuantizedMeshPosition retainedPosition{};
			context.Check(
				PrepareMeshQuantizationContext(
					config,
					{ 1, 0, 0 },
					rejectedContext).m_Error ==
					ValidationError::ChunkOutsideLogicalCellDomain &&
					QuantizeMeshPosition(
						{},
						rejectedContext,
						retainedPosition).Succeeded() &&
					retainedPosition == QuantizedMeshPosition{},
				"Rejected mesh contexts preserve their previous prepared state");

			QuantizedMeshPosition positiveHalf{};
			QuantizedMeshPosition negativeHalf{};
			context.Check(
				QuantizeMeshPosition(
					{ 1.0f / 131072.0f, 0.0f, 0.0f },
					contextAtOrigin,
					positiveHalf).Succeeded() &&
					QuantizeMeshPosition(
						{ -1.0f / 131072.0f, 0.0f, 0.0f },
						contextAtOrigin,
						negativeHalf).Succeeded() &&
					positiveHalf ==
						QuantizedMeshPosition{ 1, 0, 0 } &&
					negativeHalf ==
						QuantizedMeshPosition{ -1, 0, 0 },
				"Mesh positions use half-away-from-zero quantization");

			VoxelWorldConfig scaledConfig = config;
			scaledConfig.m_VoxelSize = 0.25f;
			scaledConfig.m_LogicalCellBounds = {
				.m_Min = { -8, -8, -8 },
				.m_MaxExclusive = { 16, 16, 16 },
			};
			MeshQuantizationContext relativeContext;
			QuantizedMeshPosition relativePosition{};
			context.Check(
				PrepareMeshQuantizationContext(
					scaledConfig,
					{ -1, 1, 0 },
					relativeContext).Succeeded() &&
					QuantizeMeshPosition(
						{ -1.875f, 2.125f, 0.125f },
						relativeContext,
						relativePosition).Succeeded() &&
					relativePosition ==
						QuantizedMeshPosition{
							32768,
							32768,
							32768,
						},
				"Mesh positions are quantized in chunk-relative voxel units");

			QuantizedMeshNormal normal{};
			context.Check(
				QuantizeMeshNormal(
					{ -1.0f, 0.0f, 1.0f },
					normal).Succeeded() &&
					normal ==
						QuantizedMeshNormal{
							-32767,
							0,
							32767,
						},
				"SNORM16 reserves minus 32768 and maps both endpoints symmetrically");

			QuantizedMeshNormal clampedNormal{};
			context.Check(
				QuantizeMeshNormal(
					{ -2.0f, 0.0f, 2.0f },
					clampedNormal).Succeeded() &&
					clampedNormal == normal,
				"SNORM16 quantization clamps components before rounding");

			const float infinity =
				std::numeric_limits<float>::infinity();
			QuantizedMeshPosition failedPosition{
				.m_X = 10,
				.m_Y = 11,
				.m_Z = 12,
			};
			context.Check(
				QuantizeMeshPosition(
					{ infinity, 0.0f, 0.0f },
					contextAtOrigin,
					failedPosition).m_Error ==
					ValidationError::NonFiniteMeshVertex &&
					failedPosition ==
						QuantizedMeshPosition{ 10, 11, 12 },
				"Non-finite mesh positions fail without changing output");

			QuantizedMeshPosition outOfRangePosition{
				.m_X = 13,
				.m_Y = 14,
				.m_Z = 15,
			};
			context.Check(
				QuantizeMeshPosition(
					{
						std::numeric_limits<float>::max(),
						0.0f,
						0.0f,
					},
					contextAtOrigin,
					outOfRangePosition).m_Error ==
					ValidationError::MeshPositionOutOfRange &&
					outOfRangePosition ==
						QuantizedMeshPosition{ 13, 14, 15 },
				"Mesh position quantization rejects int32 overflow atomically");
		}

		void RunSyntheticMeshValidationTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config =
				MakeMeshValidationConfig();
			MeshValidationResult emptyResult{};
			context.Check(
				ValidateAndHashChunkMesh(
					{},
					config,
					{},
					emptyResult).Succeeded() &&
					emptyResult.m_ValidationHash ==
						0x6068a2326a691748ull &&
					emptyResult.m_VertexCount == 0 &&
					emptyResult.m_SectionCount == 0 &&
					emptyResult.m_IndexCount == 0 &&
					emptyResult.m_TriangleCount == 0 &&
					emptyResult.m_QuantizedBounds ==
						QuantizedMeshAabb{},
				"Canonical empty meshes match their validation golden");

			const MeshData triangle = MakeSyntheticTriangleMesh();
			MeshValidationResult triangleResult{};
			const bool triangleValid =
				ValidateAndHashChunkMesh(
					triangle,
					config,
					{},
					triangleResult).Succeeded();
			context.Check(
				triangleValid &&
					triangleResult.m_ValidationHash ==
						0xc884871a1401007dull &&
					triangleResult.m_VertexCount == 3 &&
					triangleResult.m_SectionCount == 1 &&
					triangleResult.m_IndexCount == 3 &&
					triangleResult.m_TriangleCount == 1 &&
					triangleResult.m_QuantizedBounds ==
						QuantizedMeshAabb{
							.m_Min = { 65536, 65536, 65536 },
							.m_Max = { 131072, 131072, 65536 },
						},
				"A valid synthetic triangle produces canonical counts and bounds");

			const MeshData multiMaterialMesh =
				MakeSyntheticMultiMaterialMesh();
			MeshValidationResult multiMaterialResult{};
			context.Check(
				ValidateAndHashChunkMesh(
					multiMaterialMesh,
					config,
					{},
					multiMaterialResult).Succeeded() &&
					multiMaterialResult.m_ValidationHash ==
						0x344ed3af7a4eeec1ull &&
					multiMaterialResult.m_VertexCount == 4 &&
					multiMaterialResult.m_SectionCount == 2 &&
					multiMaterialResult.m_IndexCount == 6 &&
					multiMaterialResult.m_TriangleCount == 2,
				"A valid multi-material mesh matches its section-order golden");

			VoxelWorldConfig multiChunkConfig = config;
			multiChunkConfig.m_LogicalCellBounds.m_MaxExclusive =
				{ 16, 8, 8 };
			MeshValidationResult firstChunkEmptyResult{};
			MeshValidationResult otherChunkEmptyResult{};
			VoxelWorldConfig otherConfig = config;
			otherConfig.m_SurfaceBandVoxels = 3.0f;
			MeshValidationResult otherConfigEmptyResult{};
			context.Check(
				ValidateAndHashChunkMesh(
					{},
					multiChunkConfig,
					{},
					firstChunkEmptyResult).Succeeded() &&
					ValidateAndHashChunkMesh(
						{},
						multiChunkConfig,
						{ 1, 0, 0 },
						otherChunkEmptyResult).Succeeded() &&
					ValidateAndHashChunkMesh(
						{},
						otherConfig,
						{},
						otherConfigEmptyResult).Succeeded() &&
					otherChunkEmptyResult.m_ValidationHash !=
						firstChunkEmptyResult.m_ValidationHash &&
					otherConfigEmptyResult.m_ValidationHash !=
						emptyResult.m_ValidationHash,
				"Mesh validation hashes bind both chunk coordinate and config");

			MeshValidationResult targetChunkResult{};
			context.Check(
				ValidateAndHashChunkMesh(
					triangle,
					multiChunkConfig,
					{ 1, 0, 0 },
					targetChunkResult).m_Error ==
					ValidationError::
						MeshGeometryOutsideTargetCellDomain,
				"Chunk mesh validation rejects geometry from another chunk");

			MeshData positiveBoundaryMesh{
				.m_Vertices = {
					{
						.m_Position = { 8.0f, 1.0f, 1.0f },
						.m_Normal = { 1.0f, 0.0f, 0.0f },
					},
					{
						.m_Position = { 8.0f, 2.0f, 1.0f },
						.m_Normal = { 1.0f, 0.0f, 0.0f },
					},
					{
						.m_Position = { 8.0f, 1.0f, 2.0f },
						.m_Normal = { 1.0f, 0.0f, 0.0f },
					},
				},
				.m_Sections = {
					{
						.m_Material = VoxelMaterial::Stone,
						.m_Indices = { 0, 1, 2 },
					},
				},
				.m_Bounds = {
					.m_Min = { 8.0f, 1.0f, 1.0f },
					.m_Max = { 8.0f, 2.0f, 2.0f },
				},
			};
			context.Check(
				ValidateAndHashChunkMesh(
					positiveBoundaryMesh,
					multiChunkConfig,
					{},
					targetChunkResult).Succeeded(),
				"Chunk mesh validation includes the shared positive boundary");

			MeshData oversizedBounds = triangle;
			oversizedBounds.m_Bounds.m_Max.m_X = 9.0f;
			context.Check(
				ValidateAndHashChunkMesh(
					oversizedBounds,
					multiChunkConfig,
					{},
					targetChunkResult).m_Error ==
					ValidationError::
						MeshGeometryOutsideTargetCellDomain,
				"Chunk mesh bounds cannot extend outside the target chunk");

			VoxelWorldConfig partialChunkConfig = config;
			partialChunkConfig.m_LogicalCellBounds = {
				.m_Min = { 4, 0, 0 },
				.m_MaxExclusive = { 12, 8, 8 },
			};
			MeshData outsidePositivePartialChunk = triangle;
			for (MeshVertex& vertex :
				outsidePositivePartialChunk.m_Vertices)
			{
				vertex.m_Position.m_X += 12.0f;
			}
			outsidePositivePartialChunk.m_Bounds.m_Min.m_X += 12.0f;
			outsidePositivePartialChunk.m_Bounds.m_Max.m_X += 12.0f;
			context.Check(
				ValidateAndHashChunkMesh(
					triangle,
					partialChunkConfig,
					{},
					targetChunkResult).m_Error ==
					ValidationError::
						MeshGeometryOutsideTargetCellDomain &&
					ValidateAndHashChunkMesh(
						positiveBoundaryMesh,
						partialChunkConfig,
						{},
						targetChunkResult).Succeeded() &&
					ValidateAndHashChunkMesh(
						positiveBoundaryMesh,
						partialChunkConfig,
						{ 1, 0, 0 },
						targetChunkResult).Succeeded() &&
					ValidateAndHashChunkMesh(
						outsidePositivePartialChunk,
						partialChunkConfig,
						{ 1, 0, 0 },
						targetChunkResult).m_Error ==
						ValidationError::
							MeshGeometryOutsideTargetCellDomain,
				"Partial chunks validate their logical cell intersection and shared boundary");

			constexpr float SubQuantizationOffset =
				1.0f / 262144.0f;
			MeshData subQuantizationVariant = triangle;
			for (MeshVertex& vertex :
				subQuantizationVariant.m_Vertices)
			{
				vertex.m_Position.m_X +=
					SubQuantizationOffset;
				vertex.m_Position.m_Y +=
					SubQuantizationOffset;
				vertex.m_Normal.m_X = 1.0e-7f;
			}
			subQuantizationVariant.m_Bounds.m_Min.m_X +=
				SubQuantizationOffset;
			subQuantizationVariant.m_Bounds.m_Min.m_Y +=
				SubQuantizationOffset;
			subQuantizationVariant.m_Bounds.m_Max.m_X +=
				SubQuantizationOffset;
			subQuantizationVariant.m_Bounds.m_Max.m_Y +=
				SubQuantizationOffset;
			MeshValidationResult subQuantizationResult{};
			context.Check(
				triangleValid &&
					ValidateAndHashChunkMesh(
						subQuantizationVariant,
						config,
						{},
						subQuantizationResult).Succeeded() &&
					subQuantizationResult.m_ValidationHash ==
						triangleResult.m_ValidationHash,
				"Sub-quantization float differences do not change the mesh hash");

			MeshData stoneTriangle = triangle;
			stoneTriangle.m_Sections[0].m_Material =
				VoxelMaterial::Stone;
			MeshValidationResult stoneResult{};
			context.Check(
				ValidateAndHashChunkMesh(
					stoneTriangle,
					config,
					{},
					stoneResult).Succeeded() &&
					stoneResult.m_ValidationHash !=
						triangleResult.m_ValidationHash,
				"Mesh validation hashes bind material sections");

			MeshData invalidBounds = triangle;
			invalidBounds.m_Bounds.m_Max.m_X = 1.5f;
			context.Check(
				ValidateAndHashChunkMesh(
					invalidBounds,
					config,
					{},
					triangleResult).m_Error ==
					ValidationError::InvalidMeshBounds,
				"Mesh bounds must contain every vertex");

			MeshData invalidNormal = triangle;
			invalidNormal.m_Vertices[0].m_Normal =
				{ 0.0f, 0.0f, 0.5f };
			context.Check(
				ValidateAndHashChunkMesh(
					invalidNormal,
					config,
					{},
					triangleResult).m_Error ==
					ValidationError::InvalidMeshNormal,
				"Mesh vertex normals must have unit length");

			MeshData nonFiniteNormal = triangle;
			nonFiniteNormal.m_Vertices[0].m_Normal.m_X =
				std::numeric_limits<float>::infinity();
			context.Check(
				ValidateAndHashChunkMesh(
					nonFiniteNormal,
					config,
					{},
					triangleResult).m_Error ==
					ValidationError::NonFiniteMeshVertex,
				"Mesh validation rejects non-finite normals");

			MeshData invalidIndexCount = triangle;
			invalidIndexCount.m_Sections[0].m_Indices.pop_back();
			context.Check(
				ValidateAndHashChunkMesh(
					invalidIndexCount,
					config,
					{},
					triangleResult).m_Error ==
					ValidationError::InvalidMeshIndexCount,
				"Mesh section index counts must describe whole triangles");

			MeshData invalidIndex = triangle;
			invalidIndex.m_Sections[0].m_Indices[2] = 3;
			context.Check(
				ValidateAndHashChunkMesh(
					invalidIndex,
					config,
					{},
					triangleResult).m_Error ==
					ValidationError::MeshIndexOutOfRange,
				"Mesh indices must reference existing vertices");

			MeshData canonicalDegenerate = triangle;
			canonicalDegenerate.m_Vertices[1].m_Position =
				{
					1.0f + SubQuantizationOffset,
					1.0f,
					1.0f,
				};
			context.Check(
				ValidateAndHashChunkMesh(
					canonicalDegenerate,
					config,
					{},
					triangleResult).m_Error ==
					ValidationError::DegenerateMeshTriangle,
				"Emitted triangles require three canonical positions");

			MeshData areaDegenerate = triangle;
			areaDegenerate.m_Vertices[0].m_Position =
				{ 1.0f, 1.0f, 1.0f };
			areaDegenerate.m_Vertices[1].m_Position =
				{ 2.0f, 1.0f, 1.0f };
			areaDegenerate.m_Vertices[2].m_Position =
				{ 3.0f, 1.0f, 1.0f };
			areaDegenerate.m_Bounds.m_Max =
				{ 3.0f, 1.0f, 1.0f };
			context.Check(
				ValidateAndHashChunkMesh(
					areaDegenerate,
					config,
					{},
					triangleResult).m_Error ==
					ValidationError::DegenerateMeshTriangle,
				"Distinct canonical positions do not hide zero-area triangles");

			MeshData reversedWinding = triangle;
			reversedWinding.m_Sections[0].m_Indices =
				{ 0, 2, 1 };
			MeshValidationResult unchanged{
				.m_ValidationHash = 0x123456789abcdef0ull,
				.m_VertexCount = 11,
				.m_SectionCount = 12,
				.m_IndexCount = 13,
				.m_TriangleCount = 14,
				.m_QuantizedBounds = {
					.m_Min = { 1, 2, 3 },
					.m_Max = { 4, 5, 6 },
				},
			};
			const MeshValidationResult sentinel = unchanged;
			context.Check(
				ValidateAndHashChunkMesh(
					reversedWinding,
					config,
					{},
					unchanged).m_Error ==
					ValidationError::InvalidMeshWinding &&
					unchanged.m_ValidationHash ==
						sentinel.m_ValidationHash &&
					unchanged.m_VertexCount ==
						sentinel.m_VertexCount &&
					unchanged.m_SectionCount ==
						sentinel.m_SectionCount &&
					unchanged.m_IndexCount ==
						sentinel.m_IndexCount &&
					unchanged.m_TriangleCount ==
						sentinel.m_TriangleCount &&
					unchanged.m_QuantizedBounds ==
						sentinel.m_QuantizedBounds,
				"Invalid winding fails without publishing a partial validation record");

			MeshData invalidMaterial = triangle;
			invalidMaterial.m_Sections[0].m_Material =
				VoxelMaterial::Empty;
			context.Check(
				ValidateAndHashChunkMesh(
					invalidMaterial,
					config,
					{},
					triangleResult).m_Error ==
					ValidationError::InvalidMeshSection,
				"Mesh sections reject the Empty material");

			MeshData unorderedSections = triangle;
			unorderedSections.m_Sections = {
				{
					.m_Material = VoxelMaterial::Stone,
					.m_Indices = { 0, 1, 2 },
				},
				{
					.m_Material = VoxelMaterial::Soil,
					.m_Indices = { 0, 1, 2 },
				},
			};
			context.Check(
				ValidateAndHashChunkMesh(
					unorderedSections,
					config,
					{},
					triangleResult).m_Error ==
					ValidationError::InvalidMeshSection,
				"Mesh sections use strict material enum order");
		}

		void RunReferenceTopologyContractTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			constexpr std::array<CellCornerOffset, 8>
				ExpectedCorners{
					CellCornerOffset{ 0, 0, 0 },
					CellCornerOffset{ 1, 0, 0 },
					CellCornerOffset{ 0, 1, 0 },
					CellCornerOffset{ 1, 1, 0 },
					CellCornerOffset{ 0, 0, 1 },
					CellCornerOffset{ 1, 0, 1 },
					CellCornerOffset{ 0, 1, 1 },
					CellCornerOffset{ 1, 1, 1 },
				};
			constexpr std::array<
				std::array<std::uint8_t, 4>,
				6> ExpectedTetrahedra{
					std::array<std::uint8_t, 4>{ 0, 1, 3, 7 },
					std::array<std::uint8_t, 4>{ 0, 3, 2, 7 },
					std::array<std::uint8_t, 4>{ 0, 2, 6, 7 },
					std::array<std::uint8_t, 4>{ 0, 6, 4, 7 },
					std::array<std::uint8_t, 4>{ 0, 4, 5, 7 },
					std::array<std::uint8_t, 4>{ 0, 5, 1, 7 },
				};
			constexpr std::array<
				std::array<std::uint8_t, 2>,
				6> ExpectedEdges{
					std::array<std::uint8_t, 2>{ 0, 1 },
					std::array<std::uint8_t, 2>{ 0, 2 },
					std::array<std::uint8_t, 2>{ 0, 3 },
					std::array<std::uint8_t, 2>{ 1, 2 },
					std::array<std::uint8_t, 2>{ 1, 3 },
					std::array<std::uint8_t, 2>{ 2, 3 },
				};

			context.Check(
				ReferenceCubeCornerOffsets == ExpectedCorners,
				"Reference cube corner IDs use the fixed binary offsets");
			context.Check(
				ReferenceFreudenthalTetrahedra ==
					ExpectedTetrahedra,
				"Reference cells use the fixed six Freudenthal tetrahedra");
			context.Check(
				ReferenceTetrahedronEdges == ExpectedEdges,
				"Reference tetrahedra use the fixed ab-ac-ad-bc-bd-cd edge order");

			const SampleCoordZYXLess less;
			context.Check(
				less(
					{ 100, 0, 0 },
					{ -100, 1, 0 }) &&
					less(
						{ 100, 100, 0 },
						{ -100, -100, 1 }) &&
					less(
						{ -1, 0, 0 },
						{ 0, 0, 0 }),
				"Global sample endpoint order is canonical z-y-x order");
		}

		void RunReferenceGradientTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			VoxelWorldConfig config = MakeMeshValidationConfig();
			config.m_LogicalCellBounds = {
				.m_Min = {},
				.m_MaxExclusive = { 3, 3, 3 },
			};
			std::unique_ptr<VoxelWorld> world;
			const ValidationResult createResult =
				VoxelWorld::Create(config, world);
			context.Check(
				createResult.Succeeded() && world != nullptr,
				"Reference gradient fixture creates a valid voxel world");
			if (!world)
			{
				return;
			}

			bool initialized = true;
			for (std::int32_t z = 0; z <= 3; ++z)
			{
				for (std::int32_t y = 0; y <= 3; ++y)
				{
					for (std::int32_t x = 0; x <= 3; ++x)
					{
						const std::uint8_t density =
							static_cast<std::uint8_t>(
								10 * x + 20 * y + 30 * z);
						const VoxelSample sample{
							.m_Density = density,
							.m_Material =
								density < IsoValue
									? VoxelMaterial::Empty
									: VoxelMaterial::Stone,
							.m_Damage = 0,
						};
						bool changed = false;
						if (world->WriteCurrentSample(
							{ x, y, z },
							sample,
							changed).Failed())
						{
							initialized = false;
						}
					}
				}
			}
			context.Check(
				initialized,
				"Reference gradient fixture initializes every logical sample");
			if (!initialized)
			{
				return;
			}

			const ReferenceMesher mesher(*world);
			DensityGradient centerGradient{};
			context.Check(
				mesher.ComputeSampleDensityGradient(
					{ 1, 1, 1 },
					centerGradient).Succeeded() &&
					centerGradient ==
						DensityGradient{ 20.0, 40.0, 60.0 },
				"Interior sample gradients use central density differences");

			DensityGradient minimumGradient{};
			DensityGradient maximumGradient{};
			context.Check(
				mesher.ComputeSampleDensityGradient(
					{ 0, 0, 0 },
					minimumGradient).Succeeded() &&
					mesher.ComputeSampleDensityGradient(
						{ 3, 3, 3 },
						maximumGradient).Succeeded() &&
					minimumGradient ==
						DensityGradient{ 10.0, 20.0, 30.0 } &&
					maximumGradient ==
						DensityGradient{ 10.0, 20.0, 30.0 },
				"Logical world boundaries use fixed one-sided gradients");

			DensityGradient unchanged{ 7.0, 8.0, 9.0 };
			context.Check(
				mesher.ComputeSampleDensityGradient(
					{ 4, 0, 0 },
					unchanged).m_Error ==
					ValidationError::SampleOutsideLogicalBounds &&
					unchanged == DensityGradient{ 7.0, 8.0, 9.0 },
				"Out-of-bounds gradient sampling fails atomically");
		}

		void RunReferenceInterpolationTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			VoxelWorldConfig config = MakeMeshValidationConfig();
			config.m_VoxelSize = 0.25f;
			config.m_LogicalCellBounds = {
				.m_Min = {},
				.m_MaxExclusive = { 2, 2, 2 },
			};
			std::unique_ptr<VoxelWorld> world;
			const ValidationResult createResult =
				VoxelWorld::Create(config, world);
			context.Check(
				createResult.Succeeded() && world != nullptr,
				"Reference interpolation fixture creates a valid voxel world");
			if (!world)
			{
				return;
			}

			const ReferenceMesher mesher(*world);
			const ReferenceEdgeEndpoint solid{
				.m_Coordinate = { 0, 0, 0 },
				.m_Sample = {
					.m_Density = 192,
					.m_Material = VoxelMaterial::Stone,
					.m_Damage = 0,
				},
				.m_DensityGradient = { -2.0, 0.0, 0.0 },
			};
			const ReferenceEdgeEndpoint empty{
				.m_Coordinate = { 1, 0, 0 },
				.m_Sample = {
					.m_Density = 96,
					.m_Material = VoxelMaterial::Empty,
					.m_Damage = 0,
				},
				.m_DensityGradient = { 0.0, -2.0, 0.0 },
			};

			ReferenceEdgeVertex forward{};
			ReferenceEdgeVertex reverse{};
			const bool forwardSucceeded =
				mesher.InterpolateEdge(
					solid,
					empty,
					forward).Succeeded();
			const bool reverseSucceeded =
				mesher.InterpolateEdge(
					empty,
					solid,
					reverse).Succeeded();
			context.Check(
				forwardSucceeded &&
					reverseSucceeded &&
					forward == reverse &&
					forward.m_EndpointA == SampleCoord{} &&
					forward.m_EndpointB ==
						SampleCoord{ 1, 0, 0 } &&
					NearlyEqual(
						forward.m_InterpolationT,
						2.0 / 3.0) &&
					NearlyEqual(
						forward.m_Position.m_X,
						1.0 / 6.0) &&
					NearlyEqual(
						forward.m_Position.m_Y,
						0.0) &&
					NearlyEqual(
						forward.m_Position.m_Z,
						0.0) &&
					NearlyEqual(
						forward.m_DensityGradient.m_X,
						-2.0 / 3.0) &&
					NearlyEqual(
						forward.m_DensityGradient.m_Y,
						-4.0 / 3.0) &&
					NearlyEqual(
						forward.m_DensityGradient.m_Z,
						0.0) &&
					NearlyEqual(
						forward.m_Normal.m_X,
						0.4472135954999579) &&
					NearlyEqual(
						forward.m_Normal.m_Y,
						0.8944271909999159) &&
					NearlyEqual(
						forward.m_Normal.m_Z,
						0.0),
				"Edge interpolation is input-order independent and reuses one double t");

			ReferenceEdgeEndpoint isoAtA = solid;
			isoAtA.m_Sample.m_Density = IsoValue;
			ReferenceEdgeVertex atA{};
			ReferenceEdgeEndpoint emptyAtA = empty;
			emptyAtA.m_Coordinate = { 0, 1, 0 };
			ReferenceEdgeEndpoint isoAtB = isoAtA;
			isoAtB.m_Coordinate = { 1, 1, 0 };
			ReferenceEdgeVertex atB{};
			context.Check(
				mesher.InterpolateEdge(
					isoAtA,
					empty,
					atA).Succeeded() &&
					atA.m_InterpolationT == 0.0 &&
					!std::signbit(atA.m_InterpolationT) &&
					atA.m_Position ==
						Float3{ 0.0f, 0.0f, 0.0f } &&
					mesher.InterpolateEdge(
						emptyAtA,
						isoAtB,
						atB).Succeeded() &&
					atB.m_InterpolationT == 1.0 &&
					atB.m_Position ==
						Float3{ 0.25f, 0.25f, 0.0f },
				"Exact-iso endpoints produce canonical t-zero and t-one vertices");

			ReferenceEdgeEndpoint equalA = empty;
			equalA.m_Coordinate = { 0, 0, 1 };
			ReferenceEdgeEndpoint equalB = empty;
			equalB.m_Coordinate = { 1, 0, 1 };
			ReferenceEdgeVertex unchanged{
				.m_Position = { 1.0f, 2.0f, 3.0f },
				.m_Normal = { 0.0f, 1.0f, 0.0f },
				.m_DensityGradient = { 4.0, 5.0, 6.0 },
				.m_EndpointA = { 7, 8, 9 },
				.m_EndpointB = { 10, 11, 12 },
				.m_InterpolationT = 0.75,
			};
			const ReferenceEdgeVertex sentinel = unchanged;
			context.Check(
				mesher.InterpolateEdge(
					equalA,
					equalB,
					unchanged).m_Error ==
					ValidationError::EqualDensityReferenceEdge &&
					unchanged == sentinel,
				"Equal-density reference edges fail without publishing output");

			ReferenceEdgeEndpoint nonCrossing = equalB;
			nonCrossing.m_Sample.m_Density = 32;
			context.Check(
				mesher.InterpolateEdge(
					equalA,
					nonCrossing,
					unchanged).m_Error ==
					ValidationError::NonCrossingReferenceEdge,
				"Reference interpolation rejects unequal non-crossing densities");

			ReferenceEdgeEndpoint distant = empty;
			distant.m_Coordinate = { 2, 0, 0 };
			context.Check(
				mesher.InterpolateEdge(
					solid,
					distant,
					unchanged).m_Error ==
					ValidationError::InvalidReferenceEdge,
				"Reference interpolation rejects endpoints outside one cube");

			ReferenceEdgeEndpoint nonFinite = empty;
			nonFinite.m_DensityGradient.m_X =
				std::numeric_limits<double>::infinity();
			context.Check(
				mesher.InterpolateEdge(
					solid,
					nonFinite,
					unchanged).m_Error ==
					ValidationError::NonFiniteDensityGradient,
				"Reference interpolation rejects non-finite endpoint gradients");

			ReferenceEdgeEndpoint cancellingA = solid;
			ReferenceEdgeEndpoint cancellingB = empty;
			cancellingB.m_Sample.m_Density = 64;
			cancellingA.m_DensityGradient = { -1.0, 0.0, 0.0 };
			cancellingB.m_DensityGradient = { 1.0, 0.0, 0.0 };
			context.Check(
				mesher.InterpolateEdge(
					cancellingA,
					cancellingB,
					unchanged).m_Error ==
					ValidationError::DegenerateDensityGradient,
				"Reference interpolation reports a zero interpolated gradient");
		}

		void RunReferenceTetrahedronPolygonizationTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			VoxelWorldConfig config = MakeMeshValidationConfig();
			config.m_VoxelSize = 1.0f;
			config.m_LogicalCellBounds = {
				.m_Min = {},
				.m_MaxExclusive = { 2, 2, 2 },
			};
			std::unique_ptr<VoxelWorld> world;
			const ValidationResult createResult =
				VoxelWorld::Create(config, world);
			context.Check(
				createResult.Succeeded() && world != nullptr,
				"Reference tetrahedron fixture creates a valid voxel world");
			if (!world)
			{
				return;
			}

			MeshQuantizationContext quantizationContext;
			const ValidationResult contextResult =
				PrepareMeshQuantizationContext(
					config,
					{},
					quantizationContext);
			context.Check(
				contextResult.Succeeded(),
				"Reference tetrahedron fixture prepares mesh quantization");
			if (contextResult.Failed())
			{
				return;
			}

			const ReferenceMesher mesher(*world);
			bool classificationsValid = true;
			for (std::uint8_t classification = 0;
				classification < 16;
				++classification)
			{
				const std::array<ReferenceEdgeEndpoint, 8>
					corners =
						MakeReferenceCubeCorners(classification);
				ReferenceTetrahedronPolygonization polygonization;
				const ValidationResult result =
					mesher.PolygonizeTetrahedron(
						corners,
						0,
						quantizationContext,
						polygonization);
				const std::uint8_t solidCount =
					CountTetrahedronSolidBits(classification);
				const std::uint8_t expectedTriangleCount =
					solidCount == 0 || solidCount == 4
						? 0
						: solidCount == 2
							? 2
							: 1;
				classificationsValid &=
					result.Succeeded() &&
					polygonization.m_TriangleCount ==
						expectedTriangleCount &&
					polygonization
						.m_SkippedDegenerateTriangleCount == 0 &&
					polygonization.m_Material ==
						(expectedTriangleCount == 0
							? VoxelMaterial::Empty
							: VoxelMaterial::Stone);
				for (std::uint8_t triangleIndex = 0;
					triangleIndex <
						polygonization.m_TriangleCount;
					++triangleIndex)
				{
					classificationsValid &=
						HasOutwardNormalWinding(
							polygonization
								.m_Triangles[triangleIndex]);
				}
			}
			context.Check(
				classificationsValid,
				"All sixteen tetrahedron classifications produce fixed valid topology");

			const std::array<ReferenceEdgeEndpoint, 8>
				twoSolidCorners = MakeReferenceCubeCorners(0b1001);
			ReferenceTetrahedronPolygonization twoSolid{};
			const bool twoSolidSucceeded =
				mesher.PolygonizeTetrahedron(
					twoSolidCorners,
					0,
					quantizationContext,
					twoSolid).Succeeded();
			context.Check(
				twoSolidSucceeded &&
					twoSolid.m_TriangleCount == 2 &&
					TriangleContainsCubeEdge(
						twoSolid.m_Triangles[0],
						twoSolidCorners,
						0,
						1) &&
					TriangleContainsCubeEdge(
						twoSolid.m_Triangles[0],
						twoSolidCorners,
						0,
						3) &&
					TriangleContainsCubeEdge(
						twoSolid.m_Triangles[0],
						twoSolidCorners,
						7,
						3) &&
					TriangleContainsCubeEdge(
						twoSolid.m_Triangles[1],
						twoSolidCorners,
						0,
						1) &&
					TriangleContainsCubeEdge(
						twoSolid.m_Triangles[1],
						twoSolidCorners,
						7,
						3) &&
					TriangleContainsCubeEdge(
						twoSolid.m_Triangles[1],
						twoSolidCorners,
						7,
						1),
				"Two-solid tetrahedra use the fixed perimeter and diagonal");

			std::array<ReferenceEdgeEndpoint, 8>
				materialCorners = MakeReferenceCubeCorners(0b1001);
			materialCorners[0].m_Sample.m_Density = 200;
			materialCorners[0].m_Sample.m_Material =
				VoxelMaterial::Soil;
			materialCorners[7].m_Sample.m_Density = 200;
			ReferenceTetrahedronPolygonization tiedMaterial{};
			const bool tiedMaterialSucceeded =
				mesher.PolygonizeTetrahedron(
					materialCorners,
					0,
					quantizationContext,
					tiedMaterial).Succeeded();
			materialCorners[7].m_Sample.m_Density = 201;
			ReferenceTetrahedronPolygonization denserMaterial{};
			const bool denserMaterialSucceeded =
				mesher.PolygonizeTetrahedron(
					materialCorners,
					0,
					quantizationContext,
					denserMaterial).Succeeded();
			context.Check(
				tiedMaterialSucceeded &&
					tiedMaterial.m_Material ==
						VoxelMaterial::Soil &&
					denserMaterialSucceeded &&
					denserMaterial.m_Material ==
						VoxelMaterial::Stone,
				"Tetrahedron material selection uses density then cube-corner ID");

			std::array<ReferenceEdgeEndpoint, 8>
				oneIsoCorner = MakeReferenceCubeCorners(0b0001);
			oneIsoCorner[0].m_Sample.m_Density = IsoValue;
			ReferenceTetrahedronPolygonization oneIso{};
			const bool oneIsoSucceeded =
				mesher.PolygonizeTetrahedron(
					oneIsoCorner,
					0,
					quantizationContext,
					oneIso).Succeeded();
			std::array<ReferenceEdgeEndpoint, 8>
				twoIsoCorners = MakeReferenceCubeCorners(0b1001);
			twoIsoCorners[0].m_Sample.m_Density = IsoValue;
			twoIsoCorners[7].m_Sample.m_Density = IsoValue;
			ReferenceTetrahedronPolygonization twoIso{};
			const bool twoIsoSucceeded =
				mesher.PolygonizeTetrahedron(
					twoIsoCorners,
					0,
					quantizationContext,
					twoIso).Succeeded();
			context.Check(
				oneIsoSucceeded &&
					oneIso.m_TriangleCount == 0 &&
					oneIso.m_SkippedDegenerateTriangleCount == 1 &&
					twoIsoSucceeded &&
					twoIso.m_TriangleCount == 0 &&
					twoIso.m_SkippedDegenerateTriangleCount == 2,
				"Exact-iso canonical degeneracies are skipped deterministically");

			std::array<ReferenceEdgeEndpoint, 8>
				fallbackCorners = MakeReferenceCubeCorners(0b0001);
			fallbackCorners[0].m_DensityGradient =
				{ 1.0, 0.0, 0.0 };
			fallbackCorners[1].m_DensityGradient =
				{ -1.0, 1.0, 0.0 };
			fallbackCorners[3].m_DensityGradient =
				{ -1.0, -1.0, 1.0 };
			fallbackCorners[7].m_DensityGradient =
				{ -1.0, 0.0, -1.0 };
			ReferenceTetrahedronPolygonization fallback{};
			const bool fallbackSucceeded =
				mesher.PolygonizeTetrahedron(
					fallbackCorners,
					0,
					quantizationContext,
					fallback).Succeeded();
			context.Check(
				fallbackSucceeded &&
					fallback.m_TriangleCount == 1 &&
					HasDirectionWinding(
						fallback.m_Triangles[0],
						{ 1.0, 2.0 / 3.0, 1.0 / 3.0 }),
				"Degenerate triangle gradients use the centroid winding fallback");

			ReferenceTetrahedronPolygonization unchanged{
				.m_Material = VoxelMaterial::Soil,
				.m_TriangleCount = 2,
				.m_SkippedDegenerateTriangleCount = 1,
			};
			const ReferenceTetrahedronPolygonization sentinel =
				unchanged;
			const std::array<ReferenceEdgeEndpoint, 8>
				validCorners = MakeReferenceCubeCorners(0b0001);
			MeshQuantizationContext unprepared;
			std::array<ReferenceEdgeEndpoint, 8>
				malformedCorners = validCorners;
			malformedCorners[0].m_Coordinate.m_X = 1;
			context.Check(
				mesher.PolygonizeTetrahedron(
					validCorners,
					6,
					quantizationContext,
					unchanged).m_Error ==
					ValidationError::InvalidReferenceTetrahedron &&
					unchanged == sentinel &&
					mesher.PolygonizeTetrahedron(
						validCorners,
						0,
						unprepared,
						unchanged).m_Error ==
						ValidationError::
							UnpreparedMeshQuantizationContext &&
					unchanged == sentinel &&
					mesher.PolygonizeTetrahedron(
						malformedCorners,
						0,
						quantizationContext,
						unchanged).m_Error ==
						ValidationError::
							InvalidReferenceTetrahedron &&
					unchanged == sentinel,
				"Invalid tetrahedron inputs fail without publishing output");
		}
	}

	void RunNapaVoxelMesherSelfTests(
		SelfTestContext& context) noexcept
	{
		RunMeshDataLayoutTests(context);
		RunMeshQuantizationTests(context);
		RunSyntheticMeshValidationTests(context);
		RunReferenceTopologyContractTests(context);
		RunReferenceGradientTests(context);
		RunReferenceInterpolationTests(context);
		RunReferenceTetrahedronPolygonizationTests(context);
	}
}
