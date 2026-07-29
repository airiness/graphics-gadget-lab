#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include "NapaVoxelCore/Validation/CheckedArithmetic.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace napa::voxel
{
	namespace
	{
		[[nodiscard]] constexpr std::uint64_t ComputeAxisCount(
			std::int32_t minimum,
			std::int32_t maximumExclusive) noexcept
		{
			return static_cast<std::uint64_t>(
				static_cast<std::int64_t>(maximumExclusive) -
				static_cast<std::int64_t>(minimum));
		}

		[[nodiscard]] constexpr std::optional<std::uint64_t> CheckedProduct3(
			std::uint64_t x,
			std::uint64_t y,
			std::uint64_t z) noexcept
		{
			const std::optional<std::uint64_t> xy = CheckedMul(x, y);
			return xy ? CheckedMul(*xy, z) : std::nullopt;
		}

		[[nodiscard]] constexpr bool FitsSize(
			std::uint64_t value) noexcept
		{
			return CheckedNarrow<std::size_t>(value).has_value();
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

		[[nodiscard]] bool HasRepresentableChunkLocalPositions(
			const VoxelWorldConfig& config) noexcept
		{
			const double maximumLocalPosition =
				static_cast<double>(config.m_ChunkCellCount) *
				static_cast<double>(config.m_VoxelSize);
			if (!std::isfinite(maximumLocalPosition) ||
				maximumLocalPosition >
					static_cast<double>(
						std::numeric_limits<float>::max()))
			{
				return false;
			}

			const float roundedMaximumLocalPosition =
				static_cast<float>(maximumLocalPosition);
			const float nextLocalPosition = std::nextafter(
				roundedMaximumLocalPosition,
				std::numeric_limits<float>::infinity());
			if (!std::isfinite(nextLocalPosition))
			{
				return false;
			}

			const double maximumFloatSpacing =
				static_cast<double>(nextLocalPosition) -
				static_cast<double>(
					roundedMaximumLocalPosition);
			const double canonicalPositionSpacing =
				static_cast<double>(config.m_VoxelSize) /
				CanonicalPositionQuantizationScale;
			return
				maximumFloatSpacing <=
					canonicalPositionSpacing;
		}
	}

	ValidationResult ComputeLogicalDomainMetrics(
		const VoxelWorldConfig& config,
		LogicalDomainMetrics& metrics) noexcept
	{
		if (!IsSupportedChunkCellCount(config.m_ChunkCellCount))
		{
			return { ValidationError::InvalidChunkCellCount };
		}
		if (config.m_LogicalCellBounds.IsEmpty())
		{
			return { ValidationError::EmptyLogicalCellBounds };
		}

		SampleAabb sampleBounds{};
		const ValidationResult sampleBoundsResult =
			LogicalCellBoundsToSampleBounds(
				config.m_LogicalCellBounds,
				sampleBounds);
		if (sampleBoundsResult.Failed())
		{
			return sampleBoundsResult;
		}

		LogicalDomainMetrics computed{};
		computed.m_CellCountX = ComputeAxisCount(
			config.m_LogicalCellBounds.m_Min.m_X,
			config.m_LogicalCellBounds.m_MaxExclusive.m_X);
		computed.m_CellCountY = ComputeAxisCount(
			config.m_LogicalCellBounds.m_Min.m_Y,
			config.m_LogicalCellBounds.m_MaxExclusive.m_Y);
		computed.m_CellCountZ = ComputeAxisCount(
			config.m_LogicalCellBounds.m_Min.m_Z,
			config.m_LogicalCellBounds.m_MaxExclusive.m_Z);

		const std::optional<std::uint64_t> sampleCountX =
			CheckedAdd(computed.m_CellCountX, std::uint64_t{ 1 });
		const std::optional<std::uint64_t> sampleCountY =
			CheckedAdd(computed.m_CellCountY, std::uint64_t{ 1 });
		const std::optional<std::uint64_t> sampleCountZ =
			CheckedAdd(computed.m_CellCountZ, std::uint64_t{ 1 });
		if (!sampleCountX || !sampleCountY || !sampleCountZ)
		{
			return { ValidationError::LogicalSampleCountOverflow };
		}
		computed.m_SampleCountX = *sampleCountX;
		computed.m_SampleCountY = *sampleCountY;
		computed.m_SampleCountZ = *sampleCountZ;

		const std::optional<std::uint64_t> totalCellCount =
			CheckedProduct3(
				computed.m_CellCountX,
				computed.m_CellCountY,
				computed.m_CellCountZ);
		if (!totalCellCount)
		{
			return { ValidationError::LogicalCellCountOverflow };
		}
		computed.m_TotalCellCount = *totalCellCount;

		const std::optional<std::uint64_t> totalSampleCount =
			CheckedProduct3(
				computed.m_SampleCountX,
				computed.m_SampleCountY,
				computed.m_SampleCountZ);
		if (!totalSampleCount)
		{
			return { ValidationError::LogicalSampleCountOverflow };
		}
		computed.m_TotalSampleCount = *totalSampleCount;

		const ValidationResult sampleOwnerBoundsResult =
			SampleBoundsToOwnerChunkBounds(
				sampleBounds,
				config.m_ChunkCellCount,
				computed.m_SampleOwnerChunkBounds);
		if (sampleOwnerBoundsResult.Failed())
		{
			return sampleOwnerBoundsResult;
		}

		const std::optional<std::int32_t> maximumCellX = CheckedAdd(
			config.m_LogicalCellBounds.m_MaxExclusive.m_X,
			std::int32_t{ -1 });
		const std::optional<std::int32_t> maximumCellY = CheckedAdd(
			config.m_LogicalCellBounds.m_MaxExclusive.m_Y,
			std::int32_t{ -1 });
		const std::optional<std::int32_t> maximumCellZ = CheckedAdd(
			config.m_LogicalCellBounds.m_MaxExclusive.m_Z,
			std::int32_t{ -1 });
		if (!maximumCellX || !maximumCellY || !maximumCellZ)
		{
			return { ValidationError::ArithmeticOverflow };
		}

		const std::optional<std::int32_t> minimumChunkX = FloorDiv(
			config.m_LogicalCellBounds.m_Min.m_X,
			config.m_ChunkCellCount);
		const std::optional<std::int32_t> minimumChunkY = FloorDiv(
			config.m_LogicalCellBounds.m_Min.m_Y,
			config.m_ChunkCellCount);
		const std::optional<std::int32_t> minimumChunkZ = FloorDiv(
			config.m_LogicalCellBounds.m_Min.m_Z,
			config.m_ChunkCellCount);
		const std::optional<std::int32_t> maximumChunkX = FloorDiv(
			*maximumCellX,
			config.m_ChunkCellCount);
		const std::optional<std::int32_t> maximumChunkY = FloorDiv(
			*maximumCellY,
			config.m_ChunkCellCount);
		const std::optional<std::int32_t> maximumChunkZ = FloorDiv(
			*maximumCellZ,
			config.m_ChunkCellCount);
		if (!minimumChunkX || !minimumChunkY || !minimumChunkZ ||
			!maximumChunkX || !maximumChunkY || !maximumChunkZ)
		{
			return { ValidationError::ArithmeticOverflow };
		}

		const std::optional<std::int32_t> maximumChunkExclusiveX =
			CheckedAdd(*maximumChunkX, std::int32_t{ 1 });
		const std::optional<std::int32_t> maximumChunkExclusiveY =
			CheckedAdd(*maximumChunkY, std::int32_t{ 1 });
		const std::optional<std::int32_t> maximumChunkExclusiveZ =
			CheckedAdd(*maximumChunkZ, std::int32_t{ 1 });
		if (!maximumChunkExclusiveX ||
			!maximumChunkExclusiveY ||
			!maximumChunkExclusiveZ)
		{
			return { ValidationError::LogicalChunkCountOverflow };
		}

		computed.m_CellOwnerChunkBounds = {
			.m_Min = {
				*minimumChunkX,
				*minimumChunkY,
				*minimumChunkZ,
			},
			.m_MaxExclusive = {
				*maximumChunkExclusiveX,
				*maximumChunkExclusiveY,
				*maximumChunkExclusiveZ,
			},
		};

		const std::uint64_t chunkCountX = ComputeAxisCount(
			*minimumChunkX,
			*maximumChunkExclusiveX);
		const std::uint64_t chunkCountY = ComputeAxisCount(
			*minimumChunkY,
			*maximumChunkExclusiveY);
		const std::uint64_t chunkCountZ = ComputeAxisCount(
			*minimumChunkZ,
			*maximumChunkExclusiveZ);
		const std::optional<std::uint64_t> chunkCount =
			CheckedProduct3(chunkCountX, chunkCountY, chunkCountZ);
		if (!chunkCount)
		{
			return { ValidationError::LogicalChunkCountOverflow };
		}
		computed.m_CellOwnerChunkCount = *chunkCount;

		if (!FitsSize(computed.m_TotalCellCount) ||
			!FitsSize(computed.m_TotalSampleCount) ||
			!FitsSize(computed.m_CellOwnerChunkCount))
		{
			return { ValidationError::LogicalDomainSizeOverflow };
		}

		metrics = computed;
		return {};
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
		if (!HasRepresentableChunkStorageCapacity(config.m_ChunkCellCount))
		{
			return { ValidationError::ArithmeticOverflow };
		}

		LogicalDomainMetrics metrics{};
		const ValidationResult metricsResult =
			ComputeLogicalDomainMetrics(config, metrics);
		if (metricsResult.Failed())
		{
			return metricsResult;
		}
		if (!HasRepresentableChunkLocalPositions(config))
		{
			return {
				ValidationError::
					UnrepresentableChunkLocalPosition,
			};
		}
		return {};
	}
}
