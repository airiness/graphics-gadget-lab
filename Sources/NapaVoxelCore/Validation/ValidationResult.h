#pragma once

#include <cstdint>

namespace napa::voxel
{
	enum class ValidationError : std::uint8_t
	{
		None = 0,
		InvalidChunkCellCount = 1,
		NonFiniteVoxelSize = 2,
		NonPositiveVoxelSize = 3,
		NonFiniteSurfaceBandVoxels = 4,
		NonPositiveSurfaceBandVoxels = 5,
		EmptyLogicalCellBounds = 6,
		LogicalSampleBoundsOverflow = 7,
		ArithmeticOverflow = 8,
		InvalidLocalCoordinate = 9,
		InvalidCellCornerOffset = 10,
		FlatIndexOutOfRange = 11,
		CoordinateOutOfRange = 12,
		InvalidVoxelMaterial = 13,
		NonCanonicalVoxelSample = 14,
		LogicalCellCountOverflow = 15,
		LogicalSampleCountOverflow = 16,
		LogicalChunkCountOverflow = 17,
		LogicalDomainSizeOverflow = 18,
		SampleOutsideLogicalBounds = 19,
		EmptySampleBounds = 20,
		ChunkOutsideLogicalSampleDomain = 21,
		InvalidPrimitiveShape = 22,
		EmptyPrimitiveMaterial = 23,
		NonFinitePrimitivePosition = 24,
		NonFinitePrimitiveSize = 25,
		NonPositiveSphereRadius = 26,
		NonPositivePrimitiveExtent = 27,
		DuplicatePrimitiveStableId = 28,
		NonFiniteSignedDistance = 29,
		NonFiniteQuantizationInput = 30,
		EmptySafetyMarginViolation = 31,
		OriginalStateSealed = 32,
		UnpreparedDensityQuantizationContext = 33,
		UnpreparedMeshQuantizationContext = 34,
		NonFiniteMeshVertex = 35,
		MeshPositionOutOfRange = 36,
		InvalidMeshNormal = 37,
		InvalidMeshBounds = 38,
		InvalidMeshSection = 39,
		InvalidMeshIndexCount = 40,
		MeshIndexOutOfRange = 41,
		DegenerateMeshTriangle = 42,
		InvalidMeshWinding = 43,
		ChunkOutsideLogicalCellDomain = 44,
		MeshGeometryOutsideTargetChunk = 45,
		InvalidReferenceEdge = 46,
		NonCrossingReferenceEdge = 47,
		EqualDensityReferenceEdge = 48,
		NonFiniteDensityGradient = 49,
		DegenerateDensityGradient = 50,
	};

	struct ValidationResult
	{
		ValidationError m_Error = ValidationError::None;

		[[nodiscard]] constexpr bool Succeeded() const noexcept
		{
			return m_Error == ValidationError::None;
		}

		[[nodiscard]] constexpr bool Failed() const noexcept
		{
			return !Succeeded();
		}

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return Succeeded();
		}
	};
}
