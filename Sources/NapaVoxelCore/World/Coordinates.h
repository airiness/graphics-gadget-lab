#pragma once

#include "NapaVoxelCore/Validation/ValidationResult.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace napa::voxel
{
	struct SampleCoord
	{
		std::int32_t m_X = 0;
		std::int32_t m_Y = 0;
		std::int32_t m_Z = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const SampleCoord&,
			const SampleCoord&) noexcept = default;
	};

	struct CellCoord
	{
		std::int32_t m_X = 0;
		std::int32_t m_Y = 0;
		std::int32_t m_Z = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const CellCoord&,
			const CellCoord&) noexcept = default;
	};

	struct ChunkCoord
	{
		std::int32_t m_X = 0;
		std::int32_t m_Y = 0;
		std::int32_t m_Z = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const ChunkCoord&,
			const ChunkCoord&) noexcept = default;
	};

	struct LocalCoord
	{
		std::uint32_t m_X = 0;
		std::uint32_t m_Y = 0;
		std::uint32_t m_Z = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const LocalCoord&,
			const LocalCoord&) noexcept = default;
	};

	struct CellCornerOffset
	{
		std::uint32_t m_X = 0;
		std::uint32_t m_Y = 0;
		std::uint32_t m_Z = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const CellCornerOffset&,
			const CellCornerOffset&) noexcept = default;
	};

	struct SampleAabb
	{
		SampleCoord m_Min{};
		SampleCoord m_MaxExclusive{};

		[[nodiscard]] constexpr bool IsEmpty() const noexcept
		{
			return
				m_Min.m_X >= m_MaxExclusive.m_X ||
				m_Min.m_Y >= m_MaxExclusive.m_Y ||
				m_Min.m_Z >= m_MaxExclusive.m_Z;
		}

		[[nodiscard]] constexpr bool Contains(
			SampleCoord coordinate) const noexcept
		{
			return
				coordinate.m_X >= m_Min.m_X &&
				coordinate.m_Y >= m_Min.m_Y &&
				coordinate.m_Z >= m_Min.m_Z &&
				coordinate.m_X < m_MaxExclusive.m_X &&
				coordinate.m_Y < m_MaxExclusive.m_Y &&
				coordinate.m_Z < m_MaxExclusive.m_Z;
		}

		[[nodiscard]] constexpr bool ContainsBounds(
			const SampleAabb& bounds) const noexcept
		{
			return
				bounds.m_Min.m_X >= m_Min.m_X &&
				bounds.m_Min.m_Y >= m_Min.m_Y &&
				bounds.m_Min.m_Z >= m_Min.m_Z &&
				bounds.m_MaxExclusive.m_X <= m_MaxExclusive.m_X &&
				bounds.m_MaxExclusive.m_Y <= m_MaxExclusive.m_Y &&
				bounds.m_MaxExclusive.m_Z <= m_MaxExclusive.m_Z;
		}

		[[nodiscard]] friend constexpr bool operator==(
			const SampleAabb&,
			const SampleAabb&) noexcept = default;
	};

	struct CellAabb
	{
		CellCoord m_Min{};
		CellCoord m_MaxExclusive{};

		[[nodiscard]] constexpr bool IsEmpty() const noexcept
		{
			return
				m_Min.m_X >= m_MaxExclusive.m_X ||
				m_Min.m_Y >= m_MaxExclusive.m_Y ||
				m_Min.m_Z >= m_MaxExclusive.m_Z;
		}

		[[nodiscard]] constexpr bool Contains(
			CellCoord coordinate) const noexcept
		{
			return
				coordinate.m_X >= m_Min.m_X &&
				coordinate.m_Y >= m_Min.m_Y &&
				coordinate.m_Z >= m_Min.m_Z &&
				coordinate.m_X < m_MaxExclusive.m_X &&
				coordinate.m_Y < m_MaxExclusive.m_Y &&
				coordinate.m_Z < m_MaxExclusive.m_Z;
		}

		[[nodiscard]] friend constexpr bool operator==(
			const CellAabb&,
			const CellAabb&) noexcept = default;
	};

	struct ChunkAabb
	{
		ChunkCoord m_Min{};
		ChunkCoord m_MaxExclusive{};

		[[nodiscard]] constexpr bool IsEmpty() const noexcept
		{
			return
				m_Min.m_X >= m_MaxExclusive.m_X ||
				m_Min.m_Y >= m_MaxExclusive.m_Y ||
				m_Min.m_Z >= m_MaxExclusive.m_Z;
		}

		[[nodiscard]] constexpr bool Contains(
			ChunkCoord coordinate) const noexcept
		{
			return
				coordinate.m_X >= m_Min.m_X &&
				coordinate.m_Y >= m_Min.m_Y &&
				coordinate.m_Z >= m_Min.m_Z &&
				coordinate.m_X < m_MaxExclusive.m_X &&
				coordinate.m_Y < m_MaxExclusive.m_Y &&
				coordinate.m_Z < m_MaxExclusive.m_Z;
		}

		[[nodiscard]] friend constexpr bool operator==(
			const ChunkAabb&,
			const ChunkAabb&) noexcept = default;
	};

	struct OwnedSampleAddress
	{
		ChunkCoord m_Owner{};
		LocalCoord m_Local{};

		[[nodiscard]] friend constexpr bool operator==(
			const OwnedSampleAddress&,
			const OwnedSampleAddress&) noexcept = default;
	};

	struct OwnedCellAddress
	{
		ChunkCoord m_Owner{};
		LocalCoord m_Local{};

		[[nodiscard]] friend constexpr bool operator==(
			const OwnedCellAddress&,
			const OwnedCellAddress&) noexcept = default;
	};

	struct ChunkCoordZYXLess
	{
		[[nodiscard]] bool operator()(
			ChunkCoord lhs,
			ChunkCoord rhs) const noexcept;
	};

	struct SampleCoordZYXLess
	{
		[[nodiscard]] bool operator()(
			SampleCoord lhs,
			SampleCoord rhs) const noexcept;
	};

	[[nodiscard]] constexpr bool IsSupportedChunkCellCount(
		std::uint32_t chunkCellCount) noexcept
	{
		return
			chunkCellCount == 8 ||
			chunkCellCount == 16 ||
			chunkCellCount == 32;
	}

	[[nodiscard]] std::optional<std::int32_t> FloorDiv(
		std::int32_t value,
		std::uint32_t positiveDivisor) noexcept;
	[[nodiscard]] std::optional<std::uint32_t> FloorMod(
		std::int32_t value,
		std::uint32_t positiveDivisor) noexcept;

	[[nodiscard]] ValidationResult ValidateLocalCoord(
		LocalCoord local,
		std::uint32_t chunkCellCount) noexcept;
	[[nodiscard]] ValidationResult ValidateCellCornerOffset(
		CellCornerOffset corner) noexcept;

	[[nodiscard]] ValidationResult ResolveSampleOwner(
		SampleCoord sample,
		std::uint32_t chunkCellCount,
		OwnedSampleAddress& address) noexcept;
	[[nodiscard]] ValidationResult ResolveCellOwner(
		CellCoord cell,
		std::uint32_t chunkCellCount,
		OwnedCellAddress& address) noexcept;

	[[nodiscard]] ValidationResult FlattenLocal(
		LocalCoord local,
		std::uint32_t chunkCellCount,
		std::size_t& flatIndex) noexcept;
	[[nodiscard]] ValidationResult UnflattenLocal(
		std::size_t flatIndex,
		std::uint32_t chunkCellCount,
		LocalCoord& local) noexcept;

	[[nodiscard]] ValidationResult ChunkLocalToGlobalSample(
		ChunkCoord chunk,
		LocalCoord local,
		std::uint32_t chunkCellCount,
		SampleCoord& sample) noexcept;
	[[nodiscard]] ValidationResult ChunkLocalToGlobalCell(
		ChunkCoord chunk,
		LocalCoord local,
		std::uint32_t chunkCellCount,
		CellCoord& cell) noexcept;
	[[nodiscard]] ValidationResult CellCornerToGlobalSample(
		CellCoord cell,
		CellCornerOffset corner,
		SampleCoord& sample) noexcept;
	[[nodiscard]] ValidationResult LogicalCellBoundsToSampleBounds(
		const CellAabb& cellBounds,
		SampleAabb& sampleBounds) noexcept;
	[[nodiscard]] ValidationResult SampleBoundsToOwnerChunkBounds(
		const SampleAabb& sampleBounds,
		std::uint32_t chunkCellCount,
		ChunkAabb& chunkBounds) noexcept;
}
