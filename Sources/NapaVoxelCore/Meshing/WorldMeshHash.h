#pragma once

#include "NapaVoxelCore/Meshing/ChunkMeshRecord.h"
#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <cstdint>
#include <span>

namespace napa::voxel
{
	struct WorldMeshValidationResult
	{
		std::uint64_t m_ValidationHash = 0;
		std::uint64_t m_ChunkCount = 0;
		std::uint64_t m_VertexCount = 0;
		std::uint64_t m_SectionCount = 0;
		std::uint64_t m_IndexCount = 0;
		std::uint64_t m_TriangleCount = 0;
		std::uint64_t m_SkippedDegenerateTriangleCount = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const WorldMeshValidationResult&,
			const WorldMeshValidationResult&) noexcept = default;
	};

	// Records must cover the complete Cell-owner Chunk Domain in canonical
	// z/y/x order, including empty Chunk meshes.
	[[nodiscard]] ValidationResult ValidateAndHashWorldMeshRecords(
		std::span<const ChunkMeshRecord> records,
		const VoxelWorldConfig& config,
		WorldMeshValidationResult& result);
}
