#pragma once

#include "NapaVoxelCore/Meshing/MeshValidation.h"
#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace napa::voxel
{
	inline constexpr std::int64_t BoundaryContourPositionScale = 65536;
	inline constexpr std::int32_t BoundaryContourNormalTolerance = 1;

	enum class ChunkBoundaryFace : std::uint8_t
	{
		NegativeX = 0,
		PositiveX,
		NegativeY,
		PositiveY,
		NegativeZ,
		PositiveZ,
		Count,
	};

	inline constexpr std::size_t ChunkBoundaryFaceCount =
		static_cast<std::size_t>(ChunkBoundaryFace::Count);

	struct QuantizedBoundaryContourPosition
	{
		std::int64_t m_X = 0;
		std::int64_t m_Y = 0;
		std::int64_t m_Z = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const QuantizedBoundaryContourPosition&,
			const QuantizedBoundaryContourPosition&) noexcept = default;
	};

	struct BoundaryContourEndpoint
	{
		QuantizedBoundaryContourPosition m_Position{};
		QuantizedMeshNormal m_Normal{};

		[[nodiscard]] friend constexpr bool operator==(
			const BoundaryContourEndpoint&,
			const BoundaryContourEndpoint&) noexcept = default;
	};

	struct BoundaryContourSegment
	{
		BoundaryContourEndpoint m_EndpointA{};
		BoundaryContourEndpoint m_EndpointB{};

		[[nodiscard]] friend constexpr bool operator==(
			const BoundaryContourSegment&,
			const BoundaryContourSegment&) noexcept = default;
	};

	struct BoundaryContourRecord
	{
		ChunkBoundaryFace m_Face = ChunkBoundaryFace::NegativeX;
		std::vector<BoundaryContourSegment> m_Segments;
		std::uint64_t m_SkippedZeroLengthSegmentCount = 0;

		[[nodiscard]] friend bool operator==(
			const BoundaryContourRecord&,
			const BoundaryContourRecord&) = default;
	};

	using ChunkBoundaryContourSet =
		std::array<BoundaryContourRecord, ChunkBoundaryFaceCount>;

	struct BoundaryContourValidationResult
	{
		std::uint64_t m_ChunkRecordCount = 0;
		std::uint64_t m_ComparedFacePairCount = 0;
		std::uint64_t m_ComparedSegmentCount = 0;
		std::uint64_t m_SkippedZeroLengthSegmentCount = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const BoundaryContourValidationResult&,
			const BoundaryContourValidationResult&) noexcept = default;
	};

	struct QuantizedBoundaryContourPositionZYXLess
	{
		[[nodiscard]] constexpr bool operator()(
			QuantizedBoundaryContourPosition lhs,
			QuantizedBoundaryContourPosition rhs) const noexcept
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
	};

	struct BoundaryContourSegmentLess
	{
		[[nodiscard]] bool operator()(
			const BoundaryContourSegment& lhs,
			const BoundaryContourSegment& rhs) const noexcept;
	};

	struct ChunkMeshRecord;

	[[nodiscard]] constexpr bool IsKnownChunkBoundaryFace(
		ChunkBoundaryFace face) noexcept
	{
		return static_cast<std::uint8_t>(face) <
			static_cast<std::uint8_t>(ChunkBoundaryFace::Count);
	}

	[[nodiscard]] constexpr std::size_t GetChunkBoundaryFaceIndex(
		ChunkBoundaryFace face) noexcept
	{
		return static_cast<std::size_t>(face);
	}

	[[nodiscard]] constexpr ChunkBoundaryFace GetOppositeChunkBoundaryFace(
		ChunkBoundaryFace face) noexcept
	{
		switch (face)
		{
		case ChunkBoundaryFace::NegativeX:
			return ChunkBoundaryFace::PositiveX;
		case ChunkBoundaryFace::PositiveX:
			return ChunkBoundaryFace::NegativeX;
		case ChunkBoundaryFace::NegativeY:
			return ChunkBoundaryFace::PositiveY;
		case ChunkBoundaryFace::PositiveY:
			return ChunkBoundaryFace::NegativeY;
		case ChunkBoundaryFace::NegativeZ:
			return ChunkBoundaryFace::PositiveZ;
		case ChunkBoundaryFace::PositiveZ:
			return ChunkBoundaryFace::NegativeZ;
		case ChunkBoundaryFace::Count:
			break;
		}
		return ChunkBoundaryFace::Count;
	}

	[[nodiscard]] constexpr bool AreBoundaryContourNormalsEquivalent(
		QuantizedMeshNormal lhs,
		QuantizedMeshNormal rhs) noexcept
	{
		const auto difference = [](std::int16_t a, std::int16_t b)
			{
				const std::int32_t wideA = a;
				const std::int32_t wideB = b;
				return wideA >= wideB
					? wideA - wideB
					: wideB - wideA;
			};
		return
			difference(lhs.m_X, rhs.m_X) <=
				BoundaryContourNormalTolerance &&
			difference(lhs.m_Y, rhs.m_Y) <=
				BoundaryContourNormalTolerance &&
			difference(lhs.m_Z, rhs.m_Z) <=
				BoundaryContourNormalTolerance;
	}

	[[nodiscard]] ChunkBoundaryContourSet MakeEmptyChunkBoundaryContourSet();

	[[nodiscard]] ValidationResult ValidateChunkBoundaryContourSet(
		const ChunkBoundaryContourSet& contours,
		ChunkCoord chunk,
		const VoxelWorldConfig& config) noexcept;

	// Records may be a canonical subset. Every adjacent pair present in the
	// set is compared; missing-neighbor policy belongs to batch validation.
	[[nodiscard]] ValidationResult ValidateBoundaryContourSet(
		std::span<const ChunkMeshRecord> records,
		const VoxelWorldConfig& config,
		BoundaryContourValidationResult& result);
}
