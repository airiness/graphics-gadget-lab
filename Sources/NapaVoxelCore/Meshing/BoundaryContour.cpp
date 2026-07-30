#include "NapaVoxelCore/Meshing/BoundaryContour.h"

#include "NapaVoxelCore/Meshing/ChunkMeshRecord.h"
#include "NapaVoxelCore/Validation/CheckedArithmetic.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace napa::voxel
{
	namespace
	{
		[[nodiscard]] bool LessNormal(
			QuantizedMeshNormal lhs,
			QuantizedMeshNormal rhs) noexcept
		{
			if (lhs.m_X != rhs.m_X)
			{
				return lhs.m_X < rhs.m_X;
			}
			if (lhs.m_Y != rhs.m_Y)
			{
				return lhs.m_Y < rhs.m_Y;
			}
			return lhs.m_Z < rhs.m_Z;
		}

		[[nodiscard]] bool LessEndpoint(
			const BoundaryContourEndpoint& lhs,
			const BoundaryContourEndpoint& rhs) noexcept
		{
			const QuantizedBoundaryContourPositionZYXLess lessPosition{};
			if (lessPosition(lhs.m_Position, rhs.m_Position))
			{
				return true;
			}
			if (lessPosition(rhs.m_Position, lhs.m_Position))
			{
				return false;
			}
			return LessNormal(lhs.m_Normal, rhs.m_Normal);
		}

		[[nodiscard]] bool IsValidQuantizedNormal(
			QuantizedMeshNormal normal) noexcept
		{
			if (normal.m_X == std::numeric_limits<std::int16_t>::min() ||
				normal.m_Y == std::numeric_limits<std::int16_t>::min() ||
				normal.m_Z == std::numeric_limits<std::int16_t>::min())
			{
				return false;
			}
			const double x = normal.m_X;
			const double y = normal.m_Y;
			const double z = normal.m_Z;
			const double length =
				std::sqrt(x * x + y * y + z * z) /
				MeshNormalQuantizationScale;
			return
				std::isfinite(length) &&
				std::abs(length - 1.0) <=
					MeshNormalLengthTolerance;
		}

		[[nodiscard]] bool AreSegmentsEquivalent(
			const BoundaryContourSegment& lhs,
			const BoundaryContourSegment& rhs) noexcept
		{
			return
				lhs.m_EndpointA.m_Position ==
					rhs.m_EndpointA.m_Position &&
				lhs.m_EndpointB.m_Position ==
					rhs.m_EndpointB.m_Position &&
				AreBoundaryContourNormalsEquivalent(
					lhs.m_EndpointA.m_Normal,
					rhs.m_EndpointA.m_Normal) &&
				AreBoundaryContourNormalsEquivalent(
					lhs.m_EndpointB.m_Normal,
					rhs.m_EndpointB.m_Normal);
		}

		[[nodiscard]] ValidationResult ComputeQuantizedChunkBounds(
			ChunkCoord chunk,
			std::uint32_t chunkCellCount,
			QuantizedBoundaryContourPosition& minimum,
			QuantizedBoundaryContourPosition& maximum) noexcept
		{
			const auto computeAxis =
				[chunkCellCount](
					std::int32_t chunkAxis,
					std::int64_t& minimumAxis,
					std::int64_t& maximumAxis) noexcept
				{
					const std::optional<std::int64_t> origin =
						CheckedMul(
							static_cast<std::int64_t>(chunkAxis),
							static_cast<std::int64_t>(
								chunkCellCount));
					const std::optional<std::int64_t> maximumVoxel =
						origin
							? CheckedAdd(
								*origin,
								static_cast<std::int64_t>(
									chunkCellCount))
							: std::nullopt;
					const std::optional<std::int64_t> preparedMinimum =
						origin
							? CheckedMul(
								*origin,
								BoundaryContourPositionScale)
							: std::nullopt;
					const std::optional<std::int64_t> preparedMaximum =
						maximumVoxel
							? CheckedMul(
								*maximumVoxel,
								BoundaryContourPositionScale)
							: std::nullopt;
					if (!preparedMinimum || !preparedMaximum)
					{
						return false;
					}
					minimumAxis = *preparedMinimum;
					maximumAxis = *preparedMaximum;
					return true;
				};

			QuantizedBoundaryContourPosition preparedMinimum{};
			QuantizedBoundaryContourPosition preparedMaximum{};
			if (!computeAxis(
					chunk.m_X,
					preparedMinimum.m_X,
					preparedMaximum.m_X) ||
				!computeAxis(
					chunk.m_Y,
					preparedMinimum.m_Y,
					preparedMaximum.m_Y) ||
				!computeAxis(
					chunk.m_Z,
					preparedMinimum.m_Z,
					preparedMaximum.m_Z))
			{
				return { ValidationError::ArithmeticOverflow };
			}
			minimum = preparedMinimum;
			maximum = preparedMaximum;
			return {};
		}

		[[nodiscard]] bool IsPositionInChunkFace(
			QuantizedBoundaryContourPosition position,
			ChunkBoundaryFace face,
			QuantizedBoundaryContourPosition minimum,
			QuantizedBoundaryContourPosition maximum) noexcept
		{
			const bool inside =
				position.m_X >= minimum.m_X &&
				position.m_X <= maximum.m_X &&
				position.m_Y >= minimum.m_Y &&
				position.m_Y <= maximum.m_Y &&
				position.m_Z >= minimum.m_Z &&
				position.m_Z <= maximum.m_Z;
			if (!inside)
			{
				return false;
			}

			switch (face)
			{
			case ChunkBoundaryFace::NegativeX:
				return position.m_X == minimum.m_X;
			case ChunkBoundaryFace::PositiveX:
				return position.m_X == maximum.m_X;
			case ChunkBoundaryFace::NegativeY:
				return position.m_Y == minimum.m_Y;
			case ChunkBoundaryFace::PositiveY:
				return position.m_Y == maximum.m_Y;
			case ChunkBoundaryFace::NegativeZ:
				return position.m_Z == minimum.m_Z;
			case ChunkBoundaryFace::PositiveZ:
				return position.m_Z == maximum.m_Z;
			case ChunkBoundaryFace::Count:
				break;
			}
			return false;
		}

		[[nodiscard]] ValidationResult ValidateBoundaryContourRecord(
			const BoundaryContourRecord& record,
			ChunkCoord chunk,
			const VoxelWorldConfig& config) noexcept
		{
			if (!IsKnownChunkBoundaryFace(record.m_Face))
			{
				return { ValidationError::InvalidBoundaryContour };
			}

			QuantizedBoundaryContourPosition minimum{};
			QuantizedBoundaryContourPosition maximum{};
			const ValidationResult boundsResult =
				ComputeQuantizedChunkBounds(
					chunk,
					config.m_ChunkCellCount,
					minimum,
					maximum);
			if (boundsResult.Failed())
			{
				return boundsResult;
			}

			const QuantizedBoundaryContourPositionZYXLess lessPosition{};
			const BoundaryContourSegmentLess lessSegment{};
			std::optional<BoundaryContourSegment> previous;
			for (const BoundaryContourSegment& segment :
				record.m_Segments)
			{
				if (!lessPosition(
						segment.m_EndpointA.m_Position,
						segment.m_EndpointB.m_Position) ||
					!IsPositionInChunkFace(
						segment.m_EndpointA.m_Position,
						record.m_Face,
						minimum,
						maximum) ||
					!IsPositionInChunkFace(
						segment.m_EndpointB.m_Position,
						record.m_Face,
						minimum,
						maximum) ||
					!IsValidQuantizedNormal(
						segment.m_EndpointA.m_Normal) ||
					!IsValidQuantizedNormal(
						segment.m_EndpointB.m_Normal) ||
					(previous &&
						lessSegment(segment, *previous)))
				{
					return {
						ValidationError::InvalidBoundaryContour,
					};
				}
				previous = segment;
			}
			return {};
		}

		[[nodiscard]] bool RecordsMatch(
			const BoundaryContourRecord& lhs,
			const BoundaryContourRecord& rhs) noexcept
		{
			if (GetOppositeChunkBoundaryFace(lhs.m_Face) !=
					rhs.m_Face ||
				lhs.m_SkippedZeroLengthSegmentCount !=
					rhs.m_SkippedZeroLengthSegmentCount ||
				lhs.m_Segments.size() != rhs.m_Segments.size())
			{
				return false;
			}
			for (std::size_t index = 0;
				index < lhs.m_Segments.size();
				++index)
			{
				if (!AreSegmentsEquivalent(
						lhs.m_Segments[index],
						rhs.m_Segments[index]))
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] const ChunkMeshRecord* FindRecord(
			std::span<const ChunkMeshRecord> records,
			ChunkCoord chunk) noexcept
		{
			const auto iterator = std::lower_bound(
				records.begin(),
				records.end(),
				chunk,
				[](const ChunkMeshRecord& record, ChunkCoord coordinate)
				{
					return ChunkCoordZYXLess{}(
						record.m_Chunk,
						coordinate);
				});
			return iterator != records.end() &&
					iterator->m_Chunk == chunk
				? &*iterator
				: nullptr;
		}

		[[nodiscard]] const BoundaryContourRecord& GetContour(
			const ChunkMeshRecord& record,
			ChunkBoundaryFace face) noexcept
		{
			return record.m_BoundaryContours[
				GetChunkBoundaryFaceIndex(face)];
		}
	}

	bool BoundaryContourSegmentLess::operator()(
		const BoundaryContourSegment& lhs,
		const BoundaryContourSegment& rhs) const noexcept
	{
		if (LessEndpoint(lhs.m_EndpointA, rhs.m_EndpointA))
		{
			return true;
		}
		if (LessEndpoint(rhs.m_EndpointA, lhs.m_EndpointA))
		{
			return false;
		}
		return LessEndpoint(lhs.m_EndpointB, rhs.m_EndpointB);
	}

	ChunkBoundaryContourSet MakeEmptyChunkBoundaryContourSet()
	{
		ChunkBoundaryContourSet contours{};
		for (std::size_t index = 0; index < contours.size(); ++index)
		{
			contours[index].m_Face =
				static_cast<ChunkBoundaryFace>(index);
		}
		return contours;
	}

	ValidationResult ValidateChunkBoundaryContourSet(
		const ChunkBoundaryContourSet& contours,
		ChunkCoord chunk,
		const VoxelWorldConfig& config) noexcept
	{
		const ValidationResult configResult = ValidateConfig(config);
		if (configResult.Failed())
		{
			return configResult;
		}
		for (std::size_t index = 0; index < contours.size(); ++index)
		{
			if (contours[index].m_Face !=
					static_cast<ChunkBoundaryFace>(index))
			{
				return { ValidationError::InvalidBoundaryContour };
			}
			const ValidationResult recordResult =
				ValidateBoundaryContourRecord(
					contours[index],
					chunk,
					config);
			if (recordResult.Failed())
			{
				return recordResult;
			}
		}
		return {};
	}

	ValidationResult ValidateBoundaryContourSet(
		std::span<const ChunkMeshRecord> records,
		const VoxelWorldConfig& config,
		BoundaryContourValidationResult& result)
	{
		const ValidationResult configResult = ValidateConfig(config);
		if (configResult.Failed())
		{
			return configResult;
		}
		LogicalDomainMetrics metrics{};
		const ValidationResult metricsResult =
			ComputeLogicalDomainMetrics(config, metrics);
		if (metricsResult.Failed())
		{
			return metricsResult;
		}

		BoundaryContourValidationResult validated{};
		const ChunkCoordZYXLess chunkLess{};
		std::optional<ChunkCoord> previousChunk;
		for (const ChunkMeshRecord& record : records)
		{
			if (!metrics.m_CellOwnerChunkBounds.Contains(
					record.m_Chunk) ||
				(previousChunk &&
					!chunkLess(*previousChunk, record.m_Chunk)))
			{
				return {
					ValidationError::InvalidWorldMeshRecordSet,
				};
			}
			const ValidationResult contourResult =
				ValidateChunkBoundaryContourSet(
					record.m_BoundaryContours,
					record.m_Chunk,
					config);
			if (contourResult.Failed())
			{
				return contourResult;
			}

			const std::optional<std::uint64_t> recordCount =
				CheckedAdd(
					validated.m_ChunkRecordCount,
					std::uint64_t{ 1 });
			if (!recordCount)
			{
				return { ValidationError::ArithmeticOverflow };
			}
			validated.m_ChunkRecordCount = *recordCount;
			for (const BoundaryContourRecord& contour :
				record.m_BoundaryContours)
			{
				const std::optional<std::uint64_t> skippedCount =
					CheckedAdd(
						validated
							.m_SkippedZeroLengthSegmentCount,
						contour
							.m_SkippedZeroLengthSegmentCount);
				if (!skippedCount)
				{
					return {
						ValidationError::ArithmeticOverflow,
					};
				}
				validated.m_SkippedZeroLengthSegmentCount =
					*skippedCount;
			}
			previousChunk = record.m_Chunk;
		}

		for (const ChunkMeshRecord& record : records)
		{
			const std::array positiveFaces{
				ChunkBoundaryFace::PositiveX,
				ChunkBoundaryFace::PositiveY,
				ChunkBoundaryFace::PositiveZ,
			};
			for (const ChunkBoundaryFace face : positiveFaces)
			{
				ChunkCoord neighbor = record.m_Chunk;
				switch (face)
				{
				case ChunkBoundaryFace::PositiveX:
					if (record.m_Chunk.m_X >=
						metrics.m_CellOwnerChunkBounds
								.m_MaxExclusive.m_X -
							1)
					{
						continue;
					}
					++neighbor.m_X;
					break;
				case ChunkBoundaryFace::PositiveY:
					if (record.m_Chunk.m_Y >=
						metrics.m_CellOwnerChunkBounds
								.m_MaxExclusive.m_Y -
							1)
					{
						continue;
					}
					++neighbor.m_Y;
					break;
				case ChunkBoundaryFace::PositiveZ:
					if (record.m_Chunk.m_Z >=
						metrics.m_CellOwnerChunkBounds
								.m_MaxExclusive.m_Z -
							1)
					{
						continue;
					}
					++neighbor.m_Z;
					break;
				default:
					return {
						ValidationError::InvalidBoundaryContour,
					};
				}

				const ChunkMeshRecord* const neighborRecord =
					FindRecord(records, neighbor);
				if (neighborRecord == nullptr)
				{
					continue;
				}
				const BoundaryContourRecord& contour =
					GetContour(record, face);
				const BoundaryContourRecord& neighborContour =
					GetContour(
						*neighborRecord,
						GetOppositeChunkBoundaryFace(face));
				if (!RecordsMatch(contour, neighborContour))
				{
					return {
						ValidationError::
							MismatchedBoundaryContour,
					};
				}

				const std::optional<std::uint64_t> faceCount =
					CheckedAdd(
						validated.m_ComparedFacePairCount,
						std::uint64_t{ 1 });
				const std::optional<std::uint64_t> segmentCount =
					CheckedAdd(
						validated.m_ComparedSegmentCount,
						static_cast<std::uint64_t>(
							contour.m_Segments.size()));
				if (!faceCount || !segmentCount)
				{
					return {
						ValidationError::ArithmeticOverflow,
					};
				}
				validated.m_ComparedFacePairCount = *faceCount;
				validated.m_ComparedSegmentCount = *segmentCount;
			}
		}

		result = validated;
		return {};
	}
}
