#pragma once

#include "NapaVoxelCore/Meshing/ChunkMeshRecord.h"
#include "NapaVoxelCore/Meshing/MeshData.h"
#include "NapaVoxelCore/Meshing/MeshValidation.h"
#include "NapaVoxelCore/Meshing/WorldMeshHash.h"
#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"
#include "NapaVoxelCore/World/VoxelSample.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <array>
#include <cstdint>
#include <vector>

namespace napa::voxel
{
	inline constexpr std::array<CellCornerOffset, 8>
		ReferenceCubeCornerOffsets{
			CellCornerOffset{ 0, 0, 0 },
			CellCornerOffset{ 1, 0, 0 },
			CellCornerOffset{ 0, 1, 0 },
			CellCornerOffset{ 1, 1, 0 },
			CellCornerOffset{ 0, 0, 1 },
			CellCornerOffset{ 1, 0, 1 },
			CellCornerOffset{ 0, 1, 1 },
			CellCornerOffset{ 1, 1, 1 },
		};

	inline constexpr std::array<
		std::array<std::uint8_t, 4>,
		6> ReferenceFreudenthalTetrahedra{
			std::array<std::uint8_t, 4>{ 0, 1, 3, 7 },
			std::array<std::uint8_t, 4>{ 0, 3, 2, 7 },
			std::array<std::uint8_t, 4>{ 0, 2, 6, 7 },
			std::array<std::uint8_t, 4>{ 0, 6, 4, 7 },
			std::array<std::uint8_t, 4>{ 0, 4, 5, 7 },
			std::array<std::uint8_t, 4>{ 0, 5, 1, 7 },
		};

	inline constexpr std::array<
		std::array<std::uint8_t, 2>,
		6> ReferenceTetrahedronEdges{
			std::array<std::uint8_t, 2>{ 0, 1 },
			std::array<std::uint8_t, 2>{ 0, 2 },
			std::array<std::uint8_t, 2>{ 0, 3 },
			std::array<std::uint8_t, 2>{ 1, 2 },
			std::array<std::uint8_t, 2>{ 1, 3 },
			std::array<std::uint8_t, 2>{ 2, 3 },
		};

	inline constexpr std::array<
		std::array<std::array<std::uint8_t, 3>, 2>,
		ChunkBoundaryFaceCount> ReferenceBoundaryFaceTriangles{
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

	struct DensityGradient
	{
		double m_X = 0.0;
		double m_Y = 0.0;
		double m_Z = 0.0;

		[[nodiscard]] friend constexpr bool operator==(
			const DensityGradient&,
			const DensityGradient&) noexcept = default;
	};

	struct ReferenceEdgeEndpoint
	{
		SampleCoord m_Coordinate{};
		VoxelSample m_Sample{};
		DensityGradient m_DensityGradient{};

		[[nodiscard]] friend constexpr bool operator==(
			const ReferenceEdgeEndpoint&,
			const ReferenceEdgeEndpoint&) noexcept = default;
	};

	struct ReferenceEdgeVertex
	{
		// Physical position relative to the target Chunk origin.
		Float3 m_Position{};
		Float3 m_Normal{};
		DensityGradient m_DensityGradient{};
		SampleCoord m_EndpointA{};
		SampleCoord m_EndpointB{};
		double m_InterpolationT = 0.0;

		[[nodiscard]] friend constexpr bool operator==(
			const ReferenceEdgeVertex&,
			const ReferenceEdgeVertex&) noexcept = default;
	};

	struct ReferenceTriangle
	{
		std::array<ReferenceEdgeVertex, 3> m_Vertices{};
		MeshTriangleWindingEvidence m_WindingEvidence{};

		[[nodiscard]] friend constexpr bool operator==(
			const ReferenceTriangle&,
			const ReferenceTriangle&) noexcept = default;
	};

	struct ReferenceTetrahedronPolygonization
	{
		std::array<ReferenceTriangle, 2> m_Triangles{};
		VoxelMaterial m_Material = VoxelMaterial::Empty;
		std::uint8_t m_TriangleCount = 0;
		std::uint8_t m_SkippedDegenerateTriangleCount = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const ReferenceTetrahedronPolygonization&,
			const ReferenceTetrahedronPolygonization&) noexcept =
			default;
	};

	struct ReferenceWorldMeshingResult
	{
		// Complete Cell-owner Chunk Domain in canonical z/y/x order.
		std::vector<ChunkMeshRecord> m_Chunks;
		WorldMeshValidationResult m_Validation{};
		BoundaryContourValidationResult m_BoundaryValidation{};
	};

	class ReferenceMesher final
	{
	public:
		explicit ReferenceMesher(const VoxelWorld& world) noexcept;

		[[nodiscard]] ValidationResult ComputeSampleDensityGradient(
			SampleCoord coordinate,
			DensityGradient& gradient) const noexcept;
		// Produces a physical position relative to chunk's origin.
		[[nodiscard]] ValidationResult InterpolateEdge(
			ReferenceEdgeEndpoint first,
			ReferenceEdgeEndpoint second,
			ChunkCoord chunk,
			ReferenceEdgeVertex& vertex) const noexcept;
		[[nodiscard]] ValidationResult PolygonizeTetrahedron(
			const std::array<ReferenceEdgeEndpoint, 8>& cubeCorners,
			std::uint8_t tetrahedronIndex,
			const MeshQuantizationContext& quantizationContext,
			ReferenceTetrahedronPolygonization& polygonization)
			const noexcept;
		[[nodiscard]] ValidationResult MeshChunk(
			ChunkCoord chunk,
			ChunkMeshRecord& record) const;
		[[nodiscard]] ValidationResult MeshWorld(
			ReferenceWorldMeshingResult& result) const;

	private:
		[[nodiscard]] ValidationResult
			PolygonizePreparedTetrahedron(
				const std::array<
					ReferenceEdgeEndpoint,
					8>& cubeCorners,
				std::uint8_t tetrahedronIndex,
				ChunkCoord chunk,
				const MeshQuantizationContext&
					quantizationContext,
				ReferenceTetrahedronPolygonization&
					polygonization) const noexcept;

		const VoxelWorld& m_World;
	};
}
