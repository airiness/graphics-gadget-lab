#pragma once

#include "NapaVoxelCore/Meshing/MeshData.h"
#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <cstdint>
#include <span>

namespace napa::voxel
{
	inline constexpr double MeshPositionQuantizationScale =
		CanonicalPositionQuantizationScale;
	inline constexpr double MeshNormalQuantizationScale = 32767.0;
	inline constexpr double MeshNormalLengthTolerance = 1.0e-3;
	inline constexpr double MinimumMeshTriangleDoubleAreaSquared = 1.0e-12;

	struct QuantizedMeshPosition
	{
		std::int32_t m_X = 0;
		std::int32_t m_Y = 0;
		std::int32_t m_Z = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const QuantizedMeshPosition&,
			const QuantizedMeshPosition&) noexcept = default;
	};

	struct QuantizedMeshNormal
	{
		std::int16_t m_X = 0;
		std::int16_t m_Y = 0;
		std::int16_t m_Z = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const QuantizedMeshNormal&,
			const QuantizedMeshNormal&) noexcept = default;
	};

	struct QuantizedMeshAabb
	{
		QuantizedMeshPosition m_Min{};
		QuantizedMeshPosition m_Max{};

		[[nodiscard]] friend constexpr bool operator==(
			const QuantizedMeshAabb&,
			const QuantizedMeshAabb&) noexcept = default;
	};

	struct MeshTriangleWindingEvidence
	{
		// Trusted producer evidence derived from the source density field
		// using the triangle's canonical Float3 positions.
		Float3 m_OutwardDirection{};

		[[nodiscard]] friend constexpr bool operator==(
			const MeshTriangleWindingEvidence&,
			const MeshTriangleWindingEvidence&) noexcept = default;
	};

	struct MeshValidationResult
	{
		std::uint64_t m_ValidationHash = 0;
		std::uint64_t m_VertexCount = 0;
		std::uint64_t m_SectionCount = 0;
		std::uint64_t m_IndexCount = 0;
		std::uint64_t m_TriangleCount = 0;
		QuantizedMeshAabb m_QuantizedBounds{};
	};

	class MeshQuantizationContext final
	{
	public:
		MeshQuantizationContext() = default;

		[[nodiscard]] bool IsPrepared() const noexcept;
		[[nodiscard]] bool IsCompatible(
			const VoxelWorldConfig& config,
			ChunkCoord chunk) const noexcept;
		[[nodiscard]] bool ContainsTargetCellDomain(
			QuantizedMeshPosition position) const noexcept;

	private:
		friend ValidationResult PrepareMeshQuantizationContext(
			const VoxelWorldConfig& config,
			ChunkCoord chunk,
			MeshQuantizationContext& context) noexcept;
		friend ValidationResult QuantizeMeshPosition(
			Float3 position,
			const MeshQuantizationContext& context,
			QuantizedMeshPosition& quantized) noexcept;

		double m_InverseVoxelSize = 0.0;
		QuantizedMeshPosition m_TargetCellDomainMin{};
		QuantizedMeshPosition m_TargetCellDomainMax{};
		VoxelWorldConfig m_Config{};
		ChunkCoord m_TargetChunk{};
		bool m_IsPrepared = false;
	};

	[[nodiscard]] ValidationResult PrepareMeshQuantizationContext(
		const VoxelWorldConfig& config,
		ChunkCoord chunk,
		MeshQuantizationContext& context) noexcept;
	// Input positions use target Chunk-local physical space.
	[[nodiscard]] ValidationResult QuantizeMeshPosition(
		Float3 position,
		const MeshQuantizationContext& context,
		QuantizedMeshPosition& quantized) noexcept;
	[[nodiscard]] ValidationResult QuantizeMeshNormal(
		Float3 normal,
		QuantizedMeshNormal& quantized) noexcept;
	[[nodiscard]] ValidationResult ValidateMeshTriangleArea(
		Float3 a,
		Float3 b,
		Float3 c,
		float voxelSize) noexcept;
	// Winding evidence follows material-section order, then triangle order.
	// Validation trusts its field provenance and does not include it in the
	// mesh hash. Geometry bounds are inclusive; unique source-cell ownership
	// remains a producer invariant.
	[[nodiscard]] ValidationResult ValidateAndHashChunkMesh(
		const MeshData& mesh,
		std::span<const MeshTriangleWindingEvidence> windingEvidence,
		const VoxelWorldConfig& config,
		ChunkCoord chunk,
		MeshValidationResult& result);
}
