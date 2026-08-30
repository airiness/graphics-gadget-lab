#include "NapaVoxelCore/World/Coordinates.h"

#include "NapaVoxelCore/Validation/CheckedArithmetic.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace napa::voxel
{
	namespace
	{
		template <typename GlobalCoord, typename OwnedAddress>
		[[nodiscard]] ValidationResult ResolveOwner(
			GlobalCoord global, std::uint32_t chunkCellCount, OwnedAddress& address) noexcept
		{
			if (!IsSupportedChunkCellCount(chunkCellCount))
			{
				return { ValidationError::InvalidChunkCellCount };
			}

			const std::optional<std::int32_t> chunkX = FloorDiv(global.m_X, chunkCellCount);
			const std::optional<std::int32_t> chunkY = FloorDiv(global.m_Y, chunkCellCount);
			const std::optional<std::int32_t> chunkZ = FloorDiv(global.m_Z, chunkCellCount);
			const std::optional<std::uint32_t> localX = FloorMod(global.m_X, chunkCellCount);
			const std::optional<std::uint32_t> localY = FloorMod(global.m_Y, chunkCellCount);
			const std::optional<std::uint32_t> localZ = FloorMod(global.m_Z, chunkCellCount);
			if (!chunkX || !chunkY || !chunkZ || !localX || !localY || !localZ)
			{
				return { ValidationError::ArithmeticOverflow };
			}

			address = {
				.m_Owner = { *chunkX, *chunkY, *chunkZ },
				.m_Local = { *localX, *localY, *localZ },
			};
			return {};
		}

		[[nodiscard]] std::optional<std::int32_t> ComposeGlobalAxis(
			std::int32_t chunk, std::uint32_t local, std::uint32_t chunkCellCount) noexcept
		{
			const std::int64_t wideChunk = chunk;
			const std::int64_t wideChunkCellCount = chunkCellCount;
			const std::int64_t wideLocal = local;
			const std::optional<std::int64_t> origin = CheckedMul(wideChunk, wideChunkCellCount);
			if (!origin)
			{
				return std::nullopt;
			}

			const std::optional<std::int64_t> global = CheckedAdd(*origin, wideLocal);
			return global ? CheckedNarrow<std::int32_t>(*global) : std::nullopt;
		}

		template <typename GlobalCoord>
		[[nodiscard]] ValidationResult ChunkLocalToGlobal(ChunkCoord chunk, LocalCoord local,
			std::uint32_t chunkCellCount, GlobalCoord& global) noexcept
		{
			const ValidationResult localValidation = ValidateLocalCoord(local, chunkCellCount);
			if (localValidation.Failed())
			{
				return localValidation;
			}

			const std::optional<std::int32_t> globalX =
				ComposeGlobalAxis(chunk.m_X, local.m_X, chunkCellCount);
			const std::optional<std::int32_t> globalY =
				ComposeGlobalAxis(chunk.m_Y, local.m_Y, chunkCellCount);
			const std::optional<std::int32_t> globalZ =
				ComposeGlobalAxis(chunk.m_Z, local.m_Z, chunkCellCount);
			if (!globalX || !globalY || !globalZ)
			{
				return { ValidationError::CoordinateOutOfRange };
			}

			global = { *globalX, *globalY, *globalZ };
			return {};
		}
	}

	bool ChunkCoordZYXLess::operator()(ChunkCoord lhs, ChunkCoord rhs) const noexcept
	{
		if (lhs.m_Z != rhs.m_Z)
		{
			return lhs.m_Z < rhs.m_Z;
		}
		if (lhs.m_Y != rhs.m_Y)
		{
			return lhs.m_Y < rhs.m_Y;
		}
		return lhs.m_X < rhs.m_X;
	}

	bool SampleCoordZYXLess::operator()(SampleCoord lhs, SampleCoord rhs) const noexcept
	{
		if (lhs.m_Z != rhs.m_Z)
		{
			return lhs.m_Z < rhs.m_Z;
		}
		if (lhs.m_Y != rhs.m_Y)
		{
			return lhs.m_Y < rhs.m_Y;
		}
		return lhs.m_X < rhs.m_X;
	}

	std::optional<std::int32_t> FloorDiv(std::int32_t value, std::uint32_t positiveDivisor) noexcept
	{
		if (positiveDivisor == 0)
		{
			return std::nullopt;
		}

		const std::int64_t wideValue = value;
		const std::int64_t wideDivisor = positiveDivisor;
		std::int64_t quotient = wideValue / wideDivisor;
		const bool hasRemainder = quotient * wideDivisor != wideValue;
		if (wideValue < 0 && hasRemainder)
		{
			--quotient;
		}
		return CheckedNarrow<std::int32_t>(quotient);
	}

	std::optional<std::uint32_t> FloorMod(
		std::int32_t value, std::uint32_t positiveDivisor) noexcept
	{
		if (positiveDivisor == 0)
		{
			return std::nullopt;
		}

		const std::int64_t wideDivisor = positiveDivisor;
		std::int64_t remainder = static_cast<std::int64_t>(value) % wideDivisor;
		if (remainder < 0)
		{
			remainder += wideDivisor;
		}
		return CheckedNarrow<std::uint32_t>(remainder);
	}

	ValidationResult ValidateLocalCoord(LocalCoord local, std::uint32_t chunkCellCount) noexcept
	{
		if (!IsSupportedChunkCellCount(chunkCellCount))
		{
			return { ValidationError::InvalidChunkCellCount };
		}
		if (local.m_X >= chunkCellCount || local.m_Y >= chunkCellCount ||
			local.m_Z >= chunkCellCount)
		{
			return { ValidationError::InvalidLocalCoordinate };
		}
		return {};
	}

	ValidationResult ValidateCellCornerOffset(CellCornerOffset corner) noexcept
	{
		if (corner.m_X > 1 || corner.m_Y > 1 || corner.m_Z > 1)
		{
			return { ValidationError::InvalidCellCornerOffset };
		}
		return {};
	}

	ValidationResult ResolveSampleOwner(
		SampleCoord sample, std::uint32_t chunkCellCount, OwnedSampleAddress& address) noexcept
	{
		return ResolveOwner(sample, chunkCellCount, address);
	}

	ValidationResult ResolveCellOwner(
		CellCoord cell, std::uint32_t chunkCellCount, OwnedCellAddress& address) noexcept
	{
		return ResolveOwner(cell, chunkCellCount, address);
	}

	ValidationResult FlattenLocal(
		LocalCoord local, std::uint32_t chunkCellCount, std::size_t& flatIndex) noexcept
	{
		const ValidationResult localValidation = ValidateLocalCoord(local, chunkCellCount);
		if (localValidation.Failed())
		{
			return localValidation;
		}

		const std::optional<std::size_t> n = CheckedNarrow<std::size_t>(chunkCellCount);
		const std::optional<std::size_t> x = CheckedNarrow<std::size_t>(local.m_X);
		const std::optional<std::size_t> y = CheckedNarrow<std::size_t>(local.m_Y);
		const std::optional<std::size_t> z = CheckedNarrow<std::size_t>(local.m_Z);
		if (!n || !x || !y || !z)
		{
			return { ValidationError::ArithmeticOverflow };
		}

		const std::optional<std::size_t> nTimesZ = CheckedMul(*n, *z);
		const std::optional<std::size_t> yAndZ = nTimesZ ? CheckedAdd(*y, *nTimesZ) : std::nullopt;
		const std::optional<std::size_t> nTimesYAndZ =
			yAndZ ? CheckedMul(*n, *yAndZ) : std::nullopt;
		const std::optional<std::size_t> result =
			nTimesYAndZ ? CheckedAdd(*x, *nTimesYAndZ) : std::nullopt;
		if (!result)
		{
			return { ValidationError::ArithmeticOverflow };
		}

		flatIndex = *result;
		return {};
	}

	ValidationResult UnflattenLocal(
		std::size_t flatIndex, std::uint32_t chunkCellCount, LocalCoord& local) noexcept
	{
		if (!IsSupportedChunkCellCount(chunkCellCount))
		{
			return { ValidationError::InvalidChunkCellCount };
		}

		const std::optional<std::size_t> n = CheckedNarrow<std::size_t>(chunkCellCount);
		const std::optional<std::size_t> square = n ? CheckedMul(*n, *n) : std::nullopt;
		const std::optional<std::size_t> capacity = square ? CheckedMul(*square, *n) : std::nullopt;
		if (!n || !capacity)
		{
			return { ValidationError::ArithmeticOverflow };
		}
		if (flatIndex >= *capacity)
		{
			return { ValidationError::FlatIndexOutOfRange };
		}

		const std::size_t x = flatIndex % *n;
		const std::size_t yz = flatIndex / *n;
		const std::size_t y = yz % *n;
		const std::size_t z = yz / *n;
		const std::optional<std::uint32_t> localX = CheckedNarrow<std::uint32_t>(x);
		const std::optional<std::uint32_t> localY = CheckedNarrow<std::uint32_t>(y);
		const std::optional<std::uint32_t> localZ = CheckedNarrow<std::uint32_t>(z);
		if (!localX || !localY || !localZ)
		{
			return { ValidationError::ArithmeticOverflow };
		}

		local = { *localX, *localY, *localZ };
		return {};
	}

	ValidationResult ChunkLocalToGlobalSample(ChunkCoord chunk, LocalCoord local,
		std::uint32_t chunkCellCount, SampleCoord& sample) noexcept
	{
		return ChunkLocalToGlobal(chunk, local, chunkCellCount, sample);
	}

	ValidationResult ChunkLocalToGlobalCell(
		ChunkCoord chunk, LocalCoord local, std::uint32_t chunkCellCount, CellCoord& cell) noexcept
	{
		return ChunkLocalToGlobal(chunk, local, chunkCellCount, cell);
	}

	ValidationResult CellCornerToGlobalSample(
		CellCoord cell, CellCornerOffset corner, SampleCoord& sample) noexcept
	{
		const ValidationResult cornerValidation = ValidateCellCornerOffset(corner);
		if (cornerValidation.Failed())
		{
			return cornerValidation;
		}

		const std::optional<std::int32_t> sampleX =
			CheckedAdd(cell.m_X, static_cast<std::int32_t>(corner.m_X));
		const std::optional<std::int32_t> sampleY =
			CheckedAdd(cell.m_Y, static_cast<std::int32_t>(corner.m_Y));
		const std::optional<std::int32_t> sampleZ =
			CheckedAdd(cell.m_Z, static_cast<std::int32_t>(corner.m_Z));
		if (!sampleX || !sampleY || !sampleZ)
		{
			return { ValidationError::CoordinateOutOfRange };
		}

		sample = { *sampleX, *sampleY, *sampleZ };
		return {};
	}

	ValidationResult LogicalCellBoundsToSampleBounds(
		const CellAabb& cellBounds, SampleAabb& sampleBounds) noexcept
	{
		if (cellBounds.IsEmpty())
		{
			return { ValidationError::EmptyLogicalCellBounds };
		}

		const std::optional<std::int32_t> maxX =
			CheckedAdd(cellBounds.m_MaxExclusive.m_X, std::int32_t{ 1 });
		const std::optional<std::int32_t> maxY =
			CheckedAdd(cellBounds.m_MaxExclusive.m_Y, std::int32_t{ 1 });
		const std::optional<std::int32_t> maxZ =
			CheckedAdd(cellBounds.m_MaxExclusive.m_Z, std::int32_t{ 1 });
		if (!maxX || !maxY || !maxZ)
		{
			return { ValidationError::LogicalSampleBoundsOverflow };
		}

		sampleBounds = {
			.m_Min = {
				cellBounds.m_Min.m_X,
				cellBounds.m_Min.m_Y,
				cellBounds.m_Min.m_Z,
			},
			.m_MaxExclusive = { *maxX, *maxY, *maxZ },
		};
		return {};
	}

	ValidationResult SampleBoundsToOwnerChunkBounds(const SampleAabb& sampleBounds,
		std::uint32_t chunkCellCount, ChunkAabb& chunkBounds) noexcept
	{
		if (sampleBounds.IsEmpty())
		{
			return { ValidationError::EmptySampleBounds };
		}

		const SampleCoord maximumInclusive{
			sampleBounds.m_MaxExclusive.m_X - 1,
			sampleBounds.m_MaxExclusive.m_Y - 1,
			sampleBounds.m_MaxExclusive.m_Z - 1,
		};
		OwnedSampleAddress minimumAddress{};
		OwnedSampleAddress maximumAddress{};
		const ValidationResult minimumResult =
			ResolveSampleOwner(sampleBounds.m_Min, chunkCellCount, minimumAddress);
		if (minimumResult.Failed())
		{
			return minimumResult;
		}
		const ValidationResult maximumResult =
			ResolveSampleOwner(maximumInclusive, chunkCellCount, maximumAddress);
		if (maximumResult.Failed())
		{
			return maximumResult;
		}

		const std::optional<std::int32_t> maximumExclusiveX =
			CheckedAdd(maximumAddress.m_Owner.m_X, std::int32_t{ 1 });
		const std::optional<std::int32_t> maximumExclusiveY =
			CheckedAdd(maximumAddress.m_Owner.m_Y, std::int32_t{ 1 });
		const std::optional<std::int32_t> maximumExclusiveZ =
			CheckedAdd(maximumAddress.m_Owner.m_Z, std::int32_t{ 1 });
		if (!maximumExclusiveX || !maximumExclusiveY || !maximumExclusiveZ)
		{
			return { ValidationError::LogicalChunkCountOverflow };
		}

		chunkBounds = {
			.m_Min = minimumAddress.m_Owner,
			.m_MaxExclusive = {
				*maximumExclusiveX,
				*maximumExclusiveY,
				*maximumExclusiveZ,
			},
		};
		return {};
	}

	ValidationResult IntersectCellOwnerChunk(ChunkCoord chunk, std::uint32_t chunkCellCount,
		const CellAabb& logicalCellBounds, CellAabb& intersection) noexcept
	{
		if (!IsSupportedChunkCellCount(chunkCellCount))
		{
			return { ValidationError::InvalidChunkCellCount };
		}
		if (logicalCellBounds.IsEmpty())
		{
			return { ValidationError::EmptyLogicalCellBounds };
		}

		const std::int64_t chunkSize = chunkCellCount;
		const std::optional<std::int64_t> minimumX =
			CheckedMul(static_cast<std::int64_t>(chunk.m_X), chunkSize);
		const std::optional<std::int64_t> minimumY =
			CheckedMul(static_cast<std::int64_t>(chunk.m_Y), chunkSize);
		const std::optional<std::int64_t> minimumZ =
			CheckedMul(static_cast<std::int64_t>(chunk.m_Z), chunkSize);
		const std::optional<std::int64_t> maximumX =
			minimumX ? CheckedAdd(*minimumX, chunkSize) : std::nullopt;
		const std::optional<std::int64_t> maximumY =
			minimumY ? CheckedAdd(*minimumY, chunkSize) : std::nullopt;
		const std::optional<std::int64_t> maximumZ =
			minimumZ ? CheckedAdd(*minimumZ, chunkSize) : std::nullopt;
		if (!minimumX || !minimumY || !minimumZ || !maximumX || !maximumY || !maximumZ)
		{
			return { ValidationError::ArithmeticOverflow };
		}

		const std::int64_t intersectionMinimumX =
			std::max(*minimumX, static_cast<std::int64_t>(logicalCellBounds.m_Min.m_X));
		const std::int64_t intersectionMinimumY =
			std::max(*minimumY, static_cast<std::int64_t>(logicalCellBounds.m_Min.m_Y));
		const std::int64_t intersectionMinimumZ =
			std::max(*minimumZ, static_cast<std::int64_t>(logicalCellBounds.m_Min.m_Z));
		const std::int64_t intersectionMaximumX =
			std::min(*maximumX, static_cast<std::int64_t>(logicalCellBounds.m_MaxExclusive.m_X));
		const std::int64_t intersectionMaximumY =
			std::min(*maximumY, static_cast<std::int64_t>(logicalCellBounds.m_MaxExclusive.m_Y));
		const std::int64_t intersectionMaximumZ =
			std::min(*maximumZ, static_cast<std::int64_t>(logicalCellBounds.m_MaxExclusive.m_Z));
		if (intersectionMinimumX >= intersectionMaximumX ||
			intersectionMinimumY >= intersectionMaximumY ||
			intersectionMinimumZ >= intersectionMaximumZ)
		{
			return {
				ValidationError::ChunkOutsideLogicalCellDomain,
			};
		}

		const std::optional<std::int32_t> narrowedMinimumX =
			CheckedNarrow<std::int32_t>(intersectionMinimumX);
		const std::optional<std::int32_t> narrowedMinimumY =
			CheckedNarrow<std::int32_t>(intersectionMinimumY);
		const std::optional<std::int32_t> narrowedMinimumZ =
			CheckedNarrow<std::int32_t>(intersectionMinimumZ);
		const std::optional<std::int32_t> narrowedMaximumX =
			CheckedNarrow<std::int32_t>(intersectionMaximumX);
		const std::optional<std::int32_t> narrowedMaximumY =
			CheckedNarrow<std::int32_t>(intersectionMaximumY);
		const std::optional<std::int32_t> narrowedMaximumZ =
			CheckedNarrow<std::int32_t>(intersectionMaximumZ);
		if (!narrowedMinimumX || !narrowedMinimumY || !narrowedMinimumZ || !narrowedMaximumX ||
			!narrowedMaximumY || !narrowedMaximumZ)
		{
			return { ValidationError::CoordinateOutOfRange };
		}

		intersection = {
			.m_Min = {
				*narrowedMinimumX,
				*narrowedMinimumY,
				*narrowedMinimumZ,
			},
			.m_MaxExclusive = {
				*narrowedMaximumX,
				*narrowedMaximumY,
				*narrowedMaximumZ,
			},
		};
		return {};
	}
}
