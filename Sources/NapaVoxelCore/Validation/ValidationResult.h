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
