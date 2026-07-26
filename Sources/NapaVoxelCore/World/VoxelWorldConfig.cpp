#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include "NapaVoxelCore/Validation/CheckedArithmetic.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace napa::voxel
{
	namespace
	{
		[[nodiscard]] constexpr bool CanExpandToLogicalSampleBounds(
			const CellAabb& bounds) noexcept
		{
			return
				CheckedAdd(bounds.m_MaxExclusive.m_X, std::int32_t{ 1 }).has_value() &&
				CheckedAdd(bounds.m_MaxExclusive.m_Y, std::int32_t{ 1 }).has_value() &&
				CheckedAdd(bounds.m_MaxExclusive.m_Z, std::int32_t{ 1 }).has_value();
		}

		[[nodiscard]] constexpr bool HasRepresentableChunkStorageCapacity(
			std::uint32_t chunkCellCount) noexcept
		{
			const std::optional<std::uint32_t> square =
				CheckedMul(chunkCellCount, chunkCellCount);
			if (!square)
			{
				return false;
			}

			const std::optional<std::uint32_t> cube =
				CheckedMul(*square, chunkCellCount);
			return cube && CheckedNarrow<std::size_t>(*cube).has_value();
		}
	}

	ValidationResult ValidateConfig(const VoxelWorldConfig& config) noexcept
	{
		if (!IsSupportedChunkCellCount(config.m_ChunkCellCount))
		{
			return { ValidationError::InvalidChunkCellCount };
		}
		if (!std::isfinite(config.m_VoxelSize))
		{
			return { ValidationError::NonFiniteVoxelSize };
		}
		if (config.m_VoxelSize <= 0.0f)
		{
			return { ValidationError::NonPositiveVoxelSize };
		}
		if (!std::isfinite(config.m_SurfaceBandVoxels))
		{
			return { ValidationError::NonFiniteSurfaceBandVoxels };
		}
		if (config.m_SurfaceBandVoxels <= 0.0f)
		{
			return { ValidationError::NonPositiveSurfaceBandVoxels };
		}
		if (config.m_LogicalCellBounds.IsEmpty())
		{
			return { ValidationError::EmptyLogicalCellBounds };
		}
		if (!CanExpandToLogicalSampleBounds(config.m_LogicalCellBounds))
		{
			return { ValidationError::LogicalSampleBoundsOverflow };
		}
		if (!HasRepresentableChunkStorageCapacity(config.m_ChunkCellCount))
		{
			return { ValidationError::ArithmeticOverflow };
		}

		return {};
	}
}
