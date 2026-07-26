#pragma once

#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"

#include <cstdint>

namespace napa::voxel
{
	inline constexpr std::uint8_t P0IsoValue = 128;

	struct VoxelWorldConfig
	{
		std::uint32_t m_ChunkCellCount = 16;
		float m_VoxelSize = 1.0f;
		float m_SurfaceBandVoxels = 2.0f;
		CellAabb m_LogicalCellBounds{};
	};

	[[nodiscard]] ValidationResult ValidateConfig(
		const VoxelWorldConfig& config) noexcept;
}
