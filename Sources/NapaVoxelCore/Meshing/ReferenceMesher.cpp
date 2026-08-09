#include "NapaVoxelCore/Meshing/ReferenceMesher.h"

#include "NapaVoxelCore/Field/DensityQuantization.h"
#include "NapaVoxelCore/Validation/CheckedArithmetic.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace napa::voxel
{
	namespace
	{
		enum class CoordinateAxis : std::uint8_t
		{
			X,
			Y,
			Z,
		};

		[[nodiscard]] bool IsFinite(DensityGradient gradient) noexcept
		{
			return std::isfinite(gradient.m_X) && std::isfinite(gradient.m_Y) &&
				std::isfinite(gradient.m_Z);
		}

		[[nodiscard]] bool IsFinite(Float3 value) noexcept
		{
			return std::isfinite(value.m_X) && std::isfinite(value.m_Y) && std::isfinite(value.m_Z);
		}

		[[nodiscard]] std::int32_t GetAxis(SampleCoord coordinate, CoordinateAxis axis) noexcept
		{
			switch (axis)
			{
			case CoordinateAxis::X:
				return coordinate.m_X;
			case CoordinateAxis::Y:
				return coordinate.m_Y;
			case CoordinateAxis::Z:
				return coordinate.m_Z;
			}
			return 0;
		}

		void SetAxis(SampleCoord& coordinate, CoordinateAxis axis, std::int32_t value) noexcept
		{
			switch (axis)
			{
			case CoordinateAxis::X:
				coordinate.m_X = value;
				break;
			case CoordinateAxis::Y:
				coordinate.m_Y = value;
				break;
			case CoordinateAxis::Z:
				coordinate.m_Z = value;
				break;
			}
		}

		[[nodiscard]] ValidationResult ReadDensity(
			const VoxelWorld& world, SampleCoord coordinate, std::uint8_t& density) noexcept
		{
			VoxelSample sample{};
			const ValidationResult result = world.ReadCurrentSample(coordinate, sample);
			if (result.Failed())
			{
				return result;
			}
			density = sample.m_Density;
			return {};
		}

		template <typename DensityReader>
		[[nodiscard]] ValidationResult ComputeAxisDensityGradient(const SampleAabb& bounds,
			SampleCoord coordinate, CoordinateAxis axis, std::uint8_t centerDensity,
			DensityReader&& readDensity, double& gradient) noexcept
		{
			const std::int32_t value = GetAxis(coordinate, axis);
			const std::int32_t minimum = GetAxis(bounds.m_Min, axis);
			const std::int32_t maximumExclusive = GetAxis(bounds.m_MaxExclusive, axis);

			if (value == minimum)
			{
				SampleCoord positive = coordinate;
				SetAxis(positive, axis, value + 1);
				std::uint8_t positiveDensity = 0;
				const ValidationResult positiveResult = readDensity(positive, positiveDensity);
				if (positiveResult.Failed())
				{
					return positiveResult;
				}
				gradient =
					static_cast<double>(positiveDensity) - static_cast<double>(centerDensity);
				return {};
			}

			if (value == maximumExclusive - 1)
			{
				SampleCoord negative = coordinate;
				SetAxis(negative, axis, value - 1);
				std::uint8_t negativeDensity = 0;
				const ValidationResult negativeResult = readDensity(negative, negativeDensity);
				if (negativeResult.Failed())
				{
					return negativeResult;
				}
				gradient =
					static_cast<double>(centerDensity) - static_cast<double>(negativeDensity);
				return {};
			}

			SampleCoord negative = coordinate;
			SampleCoord positive = coordinate;
			SetAxis(negative, axis, value - 1);
			SetAxis(positive, axis, value + 1);
			std::uint8_t negativeDensity = 0;
			std::uint8_t positiveDensity = 0;
			const ValidationResult negativeResult = readDensity(negative, negativeDensity);
			if (negativeResult.Failed())
			{
				return negativeResult;
			}
			const ValidationResult positiveResult = readDensity(positive, positiveDensity);
			if (positiveResult.Failed())
			{
				return positiveResult;
			}
			gradient = static_cast<double>(positiveDensity) - static_cast<double>(negativeDensity);
			return {};
		}

		[[nodiscard]] std::int64_t AbsoluteDifference(std::int32_t lhs, std::int32_t rhs) noexcept
		{
			const std::int64_t difference =
				static_cast<std::int64_t>(lhs) - static_cast<std::int64_t>(rhs);
			return difference < 0 ? -difference : difference;
		}

		[[nodiscard]] bool IsReferenceEdge(SampleCoord first, SampleCoord second) noexcept
		{
			const std::int64_t x = AbsoluteDifference(first.m_X, second.m_X);
			const std::int64_t y = AbsoluteDifference(first.m_Y, second.m_Y);
			const std::int64_t z = AbsoluteDifference(first.m_Z, second.m_Z);
			return x <= 1 && y <= 1 && z <= 1 && x + y + z > 0;
		}

		[[nodiscard]] Float3 InterpolatePosition(SampleCoord first, SampleCoord second,
			ChunkCoord chunk, std::uint32_t chunkCellCount, double interpolationT,
			double voxelSize) noexcept
		{
			const double originX = static_cast<double>(
				static_cast<std::int64_t>(chunk.m_X) * static_cast<std::int64_t>(chunkCellCount));
			const double originY = static_cast<double>(
				static_cast<std::int64_t>(chunk.m_Y) * static_cast<std::int64_t>(chunkCellCount));
			const double originZ = static_cast<double>(
				static_cast<std::int64_t>(chunk.m_Z) * static_cast<std::int64_t>(chunkCellCount));
			const double x =
				(static_cast<double>(first.m_X) - originX +
					(static_cast<double>(second.m_X) - static_cast<double>(first.m_X)) *
					interpolationT) *
				voxelSize;
			const double y =
				(static_cast<double>(first.m_Y) - originY +
					(static_cast<double>(second.m_Y) - static_cast<double>(first.m_Y)) *
					interpolationT) *
				voxelSize;
			const double z =
				(static_cast<double>(first.m_Z) - originZ +
					(static_cast<double>(second.m_Z) - static_cast<double>(first.m_Z)) *
					interpolationT) *
				voxelSize;
			return {
				static_cast<float>(x),
				static_cast<float>(y),
				static_cast<float>(z),
			};
		}

		[[nodiscard]] DensityGradient InterpolateDensityGradient(
			DensityGradient first, DensityGradient second, double interpolationT) noexcept
		{
			return {
				first.m_X + (second.m_X - first.m_X) * interpolationT,
				first.m_Y + (second.m_Y - first.m_Y) * interpolationT,
				first.m_Z + (second.m_Z - first.m_Z) * interpolationT,
			};
		}

		[[nodiscard]] ValidationResult ComputeOutwardNormal(
			DensityGradient gradient, Float3& normal) noexcept
		{
			if (!IsFinite(gradient))
			{
				return { ValidationError::NonFiniteDensityGradient };
			}

			const double lengthSquared = gradient.m_X * gradient.m_X + gradient.m_Y * gradient.m_Y +
				gradient.m_Z * gradient.m_Z;
			if (!std::isfinite(lengthSquared))
			{
				return { ValidationError::NonFiniteDensityGradient };
			}
			if (lengthSquared <= 0.0)
			{
				return { ValidationError::DegenerateDensityGradient };
			}

			const double inverseLength = 1.0 / std::sqrt(lengthSquared);
			const Float3 prepared{
				static_cast<float>(-gradient.m_X * inverseLength),
				static_cast<float>(-gradient.m_Y * inverseLength),
				static_cast<float>(-gradient.m_Z * inverseLength),
			};
			if (!IsFinite(prepared))
			{
				return { ValidationError::NonFiniteDensityGradient };
			}

			normal = prepared;
			return {};
		}

		[[nodiscard]] ValidationResult InterpolatePreparedEdgeCandidate(
			const VoxelWorldConfig& config, ChunkCoord chunk, ReferenceEdgeEndpoint first,
			ReferenceEdgeEndpoint second, ReferenceEdgeVertex& vertex) noexcept
		{
			if (SampleCoordZYXLess{}(second.m_Coordinate, first.m_Coordinate))
			{
				std::swap(first, second);
			}

			const std::int32_t densityA = first.m_Sample.m_Density;
			const std::int32_t densityB = second.m_Sample.m_Density;
			if (densityA == densityB)
			{
				return {
					ValidationError::EqualDensityReferenceEdge,
				};
			}

			const bool solidA = densityA >= IsoValue;
			const bool solidB = densityB >= IsoValue;
			if (solidA == solidB)
			{
				return {
					ValidationError::NonCrossingReferenceEdge,
				};
			}

			double interpolationT =
				(static_cast<double>(IsoValue) - static_cast<double>(densityA)) /
				(static_cast<double>(densityB) - static_cast<double>(densityA));
			interpolationT = std::clamp(interpolationT, 0.0, 1.0);
			if (interpolationT == 0.0)
			{
				interpolationT = 0.0;
			}

			const Float3 position = InterpolatePosition(first.m_Coordinate, second.m_Coordinate,
				chunk, config.m_ChunkCellCount, interpolationT,
				static_cast<double>(config.m_VoxelSize));
			if (!IsFinite(position))
			{
				return { ValidationError::NonFiniteMeshVertex };
			}

			const DensityGradient densityGradient = InterpolateDensityGradient(
				first.m_DensityGradient, second.m_DensityGradient, interpolationT);
			if (!IsFinite(densityGradient))
			{
				return {
					ValidationError::NonFiniteDensityGradient,
				};
			}

			vertex = {
				.m_Position = position,
				.m_DensityGradient = densityGradient,
				.m_EndpointA = first.m_Coordinate,
				.m_EndpointB = second.m_Coordinate,
				.m_InterpolationT = interpolationT,
			};
			return {};
		}

		[[nodiscard]] ValidationResult InterpolateEdgeCandidate(const VoxelWorld& world,
			ChunkCoord chunk, ReferenceEdgeEndpoint first, ReferenceEdgeEndpoint second,
			ReferenceEdgeVertex& vertex) noexcept
		{
			const SampleAabb bounds = world.GetLogicalSampleBounds();
			if (!bounds.Contains(first.m_Coordinate) || !bounds.Contains(second.m_Coordinate))
			{
				return {
					ValidationError::SampleOutsideLogicalBounds,
				};
			}
			if (!IsReferenceEdge(first.m_Coordinate, second.m_Coordinate))
			{
				return { ValidationError::InvalidReferenceEdge };
			}

			const ValidationResult firstSampleResult = ValidateVoxelSample(first.m_Sample);
			if (firstSampleResult.Failed())
			{
				return firstSampleResult;
			}
			const ValidationResult secondSampleResult = ValidateVoxelSample(second.m_Sample);
			if (secondSampleResult.Failed())
			{
				return secondSampleResult;
			}
			if (!IsFinite(first.m_DensityGradient) || !IsFinite(second.m_DensityGradient))
			{
				return {
					ValidationError::NonFiniteDensityGradient,
				};
			}
			return InterpolatePreparedEdgeCandidate(
				world.GetConfig(), chunk, first, second, vertex);
		}

		[[nodiscard]] ValidationResult QuantizeBoundaryPositionComponent(std::int32_t first,
			std::int32_t second, double interpolationT, std::int64_t& quantized) noexcept
		{
			const std::optional<std::int64_t> base =
				CheckedMul(static_cast<std::int64_t>(first), BoundaryContourPositionScale);
			const std::int64_t delta =
				static_cast<std::int64_t>(second) - static_cast<std::int64_t>(first);
			std::int64_t offset = 0;
			const ValidationResult roundResult =
				RoundHalfAwayFromZero(static_cast<double>(delta) * interpolationT *
					static_cast<double>(BoundaryContourPositionScale),
					offset);
			const std::optional<std::int64_t> prepared =
				base ? CheckedAdd(*base, offset) : std::nullopt;
			if (roundResult.Failed() || !prepared)
			{
				return { ValidationError::ArithmeticOverflow };
			}
			quantized = *prepared;
			return {};
		}

		[[nodiscard]] ValidationResult QuantizeBoundaryPosition(
			const ReferenceEdgeVertex& vertex, QuantizedBoundaryContourPosition& position) noexcept
		{
			QuantizedBoundaryContourPosition prepared{};
			const ValidationResult xResult =
				QuantizeBoundaryPositionComponent(vertex.m_EndpointA.m_X, vertex.m_EndpointB.m_X,
					vertex.m_InterpolationT, prepared.m_X);
			if (xResult.Failed())
			{
				return xResult;
			}
			const ValidationResult yResult =
				QuantizeBoundaryPositionComponent(vertex.m_EndpointA.m_Y, vertex.m_EndpointB.m_Y,
					vertex.m_InterpolationT, prepared.m_Y);
			if (yResult.Failed())
			{
				return yResult;
			}
			const ValidationResult zResult =
				QuantizeBoundaryPositionComponent(vertex.m_EndpointA.m_Z, vertex.m_EndpointB.m_Z,
					vertex.m_InterpolationT, prepared.m_Z);
			if (zResult.Failed())
			{
				return zResult;
			}
			position = prepared;
			return {};
		}

		[[nodiscard]] ValidationResult QuantizeBoundaryNormal(
			DensityGradient gradient, QuantizedMeshNormal& normal) noexcept
		{
			Float3 outwardNormal{};
			const ValidationResult normalResult = ComputeOutwardNormal(gradient, outwardNormal);
			if (normalResult.Failed())
			{
				return normalResult;
			}
			return QuantizeMeshNormal(outwardNormal, normal);
		}

		[[nodiscard]] ValidationResult AppendBoundaryFaceTriangleContour(
			const VoxelWorldConfig& config, ChunkCoord chunk,
			const std::array<ReferenceEdgeEndpoint, 8>& cubeCorners,
			const std::array<std::uint8_t, 3>& triangle, BoundaryContourRecord& contour)
		{
			constexpr std::array triangleEdges{
				std::array<std::uint8_t, 2>{ 0, 1 },
				std::array<std::uint8_t, 2>{ 0, 2 },
				std::array<std::uint8_t, 2>{ 1, 2 },
			};
			std::array<ReferenceEdgeVertex, 2> crossingVertices{};
			std::uint8_t crossingCount = 0;
			for (const std::array<std::uint8_t, 2>& edge : triangleEdges)
			{
				const std::uint8_t firstCornerId = triangle[edge[0]];
				const std::uint8_t secondCornerId = triangle[edge[1]];
				const bool firstSolid = cubeCorners[firstCornerId].m_Sample.m_Density >= IsoValue;
				const bool secondSolid = cubeCorners[secondCornerId].m_Sample.m_Density >= IsoValue;
				if (firstSolid == secondSolid)
				{
					continue;
				}
				if (static_cast<std::size_t>(crossingCount) >= crossingVertices.size())
				{
					return {
						ValidationError::InvalidBoundaryContour,
					};
				}
				const ValidationResult interpolationResult =
					InterpolatePreparedEdgeCandidate(config, chunk, cubeCorners[firstCornerId],
						cubeCorners[secondCornerId], crossingVertices[crossingCount]);
				if (interpolationResult.Failed())
				{
					return interpolationResult;
				}
				++crossingCount;
			}
			if (crossingCount == 0)
			{
				return {};
			}
			if (static_cast<std::size_t>(crossingCount) != crossingVertices.size())
			{
				return {
					ValidationError::InvalidBoundaryContour,
				};
			}

			std::array<QuantizedBoundaryContourPosition, 2> positions{};
			for (std::size_t index = 0; index < positions.size(); ++index)
			{
				const ValidationResult positionResult =
					QuantizeBoundaryPosition(crossingVertices[index], positions[index]);
				if (positionResult.Failed())
				{
					return positionResult;
				}
			}
			if (positions[0] == positions[1])
			{
				const std::optional<std::uint64_t> skipped =
					CheckedAdd(contour.m_SkippedZeroLengthSegmentCount, std::uint64_t{ 1 });
				if (!skipped)
				{
					return {
						ValidationError::ArithmeticOverflow,
					};
				}
				contour.m_SkippedZeroLengthSegmentCount = *skipped;
				return {};
			}

			std::array<QuantizedMeshNormal, 2> normals{};
			for (std::size_t index = 0; index < normals.size(); ++index)
			{
				const ValidationResult normalResult = QuantizeBoundaryNormal(
					crossingVertices[index].m_DensityGradient, normals[index]);
				if (normalResult.Failed())
				{
					return normalResult;
				}
			}

			BoundaryContourSegment segment{
				.m_EndpointA = {
					.m_Position = positions[0],
					.m_Normal = normals[0],
				},
				.m_EndpointB = {
					.m_Position = positions[1],
					.m_Normal = normals[1],
				},
			};
			if (QuantizedBoundaryContourPositionZYXLess{}(
				segment.m_EndpointB.m_Position, segment.m_EndpointA.m_Position))
			{
				std::swap(segment.m_EndpointA, segment.m_EndpointB);
			}
			contour.m_Segments.push_back(std::move(segment));
			return {};
		}

		[[nodiscard]] ValidationResult AppendBoundaryFaceContours(const VoxelWorldConfig& config,
			ChunkCoord chunk, const std::array<ReferenceEdgeEndpoint, 8>& cubeCorners,
			ChunkBoundaryFace face, BoundaryContourRecord& contour)
		{
			const auto& faceTriangles =
				ReferenceBoundaryFaceTriangles[GetChunkBoundaryFaceIndex(face)];
			for (const std::array<std::uint8_t, 3>& triangle : faceTriangles)
			{
				const ValidationResult result = AppendBoundaryFaceTriangleContour(
					config, chunk, cubeCorners, triangle, contour);
				if (result.Failed())
				{
					return result;
				}
			}
			return {};
		}

		[[nodiscard]] bool CellTouchesChunkBoundaryFace(const ReferenceEdgeEndpoint& minimumCorner,
			ChunkCoord chunk, std::uint32_t chunkCellCount, ChunkBoundaryFace face) noexcept
		{
			const std::int64_t originX =
				static_cast<std::int64_t>(chunk.m_X) * static_cast<std::int64_t>(chunkCellCount);
			const std::int64_t originY =
				static_cast<std::int64_t>(chunk.m_Y) * static_cast<std::int64_t>(chunkCellCount);
			const std::int64_t originZ =
				static_cast<std::int64_t>(chunk.m_Z) * static_cast<std::int64_t>(chunkCellCount);
			switch (face)
			{
			case ChunkBoundaryFace::NegativeX:
				return minimumCorner.m_Coordinate.m_X == originX;
			case ChunkBoundaryFace::PositiveX:
				return static_cast<std::int64_t>(minimumCorner.m_Coordinate.m_X) + 1 ==
					originX + chunkCellCount;
			case ChunkBoundaryFace::NegativeY:
				return minimumCorner.m_Coordinate.m_Y == originY;
			case ChunkBoundaryFace::PositiveY:
				return static_cast<std::int64_t>(minimumCorner.m_Coordinate.m_Y) + 1 ==
					originY + chunkCellCount;
			case ChunkBoundaryFace::NegativeZ:
				return minimumCorner.m_Coordinate.m_Z == originZ;
			case ChunkBoundaryFace::PositiveZ:
				return static_cast<std::int64_t>(minimumCorner.m_Coordinate.m_Z) + 1 ==
					originZ + chunkCellCount;
			case ChunkBoundaryFace::Count:
				break;
			}
			return false;
		}

		[[nodiscard]] ValidationResult AppendCellBoundaryContours(const VoxelWorldConfig& config,
			ChunkCoord chunk, const std::array<ReferenceEdgeEndpoint, 8>& cubeCorners,
			ChunkBoundaryContourSet& contours)
		{
			for (std::size_t faceIndex = 0; faceIndex < contours.size(); ++faceIndex)
			{
				const ChunkBoundaryFace face = static_cast<ChunkBoundaryFace>(faceIndex);
				if (!CellTouchesChunkBoundaryFace(
					cubeCorners[0], chunk, config.m_ChunkCellCount, face))
				{
					continue;
				}
				const ValidationResult result = AppendBoundaryFaceContours(
					config, chunk, cubeCorners, face, contours[faceIndex]);
				if (result.Failed())
				{
					return result;
				}
			}
			return {};
		}

		[[nodiscard]] ValidationResult ValidateTetrahedronCorners(const VoxelWorld& world,
			const std::array<ReferenceEdgeEndpoint, 8>& cubeCorners,
			const std::array<std::uint8_t, 4>& tetrahedron, CellCoord& cell) noexcept
		{
			const std::uint8_t firstCornerId = tetrahedron[0];
			const ReferenceEdgeEndpoint& first = cubeCorners[firstCornerId];
			const CellCornerOffset firstOffset = ReferenceCubeCornerOffsets[firstCornerId];
			const std::int64_t cellX = static_cast<std::int64_t>(first.m_Coordinate.m_X) -
				static_cast<std::int64_t>(firstOffset.m_X);
			const std::int64_t cellY = static_cast<std::int64_t>(first.m_Coordinate.m_Y) -
				static_cast<std::int64_t>(firstOffset.m_Y);
			const std::int64_t cellZ = static_cast<std::int64_t>(first.m_Coordinate.m_Z) -
				static_cast<std::int64_t>(firstOffset.m_Z);
			const SampleAabb bounds = world.GetLogicalSampleBounds();
			const std::optional<std::int32_t> narrowedCellX = CheckedNarrow<std::int32_t>(cellX);
			const std::optional<std::int32_t> narrowedCellY = CheckedNarrow<std::int32_t>(cellY);
			const std::optional<std::int32_t> narrowedCellZ = CheckedNarrow<std::int32_t>(cellZ);
			if (!narrowedCellX || !narrowedCellY || !narrowedCellZ)
			{
				return {
					ValidationError::InvalidReferenceTetrahedron,
				};
			}

			for (const std::uint8_t cornerId : tetrahedron)
			{
				const ReferenceEdgeEndpoint& corner = cubeCorners[cornerId];
				const CellCornerOffset offset = ReferenceCubeCornerOffsets[cornerId];
				if (static_cast<std::int64_t>(corner.m_Coordinate.m_X) !=
					cellX + static_cast<std::int64_t>(offset.m_X) ||
					static_cast<std::int64_t>(corner.m_Coordinate.m_Y) !=
					cellY + static_cast<std::int64_t>(offset.m_Y) ||
					static_cast<std::int64_t>(corner.m_Coordinate.m_Z) !=
					cellZ + static_cast<std::int64_t>(offset.m_Z) ||
					!bounds.Contains(corner.m_Coordinate))
				{
					return {
						ValidationError::InvalidReferenceTetrahedron,
					};
				}

				const ValidationResult sampleResult = ValidateVoxelSample(corner.m_Sample);
				if (sampleResult.Failed())
				{
					return sampleResult;
				}
				if (!IsFinite(corner.m_DensityGradient))
				{
					return {
						ValidationError::NonFiniteDensityGradient,
					};
				}
			}
			cell = {
				*narrowedCellX,
				*narrowedCellY,
				*narrowedCellZ,
			};
			return {};
		}

		[[nodiscard]] VoxelMaterial SelectTetrahedronMaterial(
			const std::array<ReferenceEdgeEndpoint, 8>& cubeCorners,
			const std::array<std::uint8_t, 4>& tetrahedron) noexcept
		{
			std::uint8_t selectedCornerId = 0;
			std::uint8_t selectedDensity = 0;
			bool selected = false;
			for (const std::uint8_t cornerId : tetrahedron)
			{
				const VoxelSample sample = cubeCorners[cornerId].m_Sample;
				if (sample.m_Density < IsoValue)
				{
					continue;
				}
				if (!selected || sample.m_Density > selectedDensity ||
					(sample.m_Density == selectedDensity && cornerId < selectedCornerId))
				{
					selectedCornerId = cornerId;
					selectedDensity = sample.m_Density;
					selected = true;
				}
			}
			return selected ? cubeCorners[selectedCornerId].m_Sample.m_Material
				: VoxelMaterial::Empty;
		}

		[[nodiscard]] DensityGradient ComputeSolidToEmptyDirection(
			const std::array<ReferenceEdgeEndpoint, 8>& cubeCorners,
			const std::array<std::uint8_t, 4>& tetrahedron) noexcept
		{
			DensityGradient solidSum{};
			DensityGradient emptySum{};
			double solidCount = 0.0;
			double emptyCount = 0.0;
			for (const std::uint8_t cornerId : tetrahedron)
			{
				const ReferenceEdgeEndpoint& corner = cubeCorners[cornerId];
				DensityGradient* const sum =
					corner.m_Sample.m_Density >= IsoValue ? &solidSum : &emptySum;
				double* const count =
					corner.m_Sample.m_Density >= IsoValue ? &solidCount : &emptyCount;
				sum->m_X += corner.m_Coordinate.m_X;
				sum->m_Y += corner.m_Coordinate.m_Y;
				sum->m_Z += corner.m_Coordinate.m_Z;
				++*count;
			}

			if (solidCount == 0.0 || emptyCount == 0.0)
			{
				return {};
			}
			return {
				emptySum.m_X / emptyCount - solidSum.m_X / solidCount,
				emptySum.m_Y / emptyCount - solidSum.m_Y / solidCount,
				emptySum.m_Z / emptyCount - solidSum.m_Z / solidCount,
			};
		}

		[[nodiscard]] ValidationResult OrientReferenceTriangle(
			const std::array<ReferenceEdgeEndpoint, 8>& cubeCorners,
			const std::array<std::uint8_t, 4>& tetrahedron, ReferenceTriangle& triangle) noexcept
		{
			const Float3 a = triangle.m_Vertices[0].m_Position;
			const Float3 b = triangle.m_Vertices[1].m_Position;
			const Float3 c = triangle.m_Vertices[2].m_Position;
			const double abX = static_cast<double>(b.m_X) - static_cast<double>(a.m_X);
			const double abY = static_cast<double>(b.m_Y) - static_cast<double>(a.m_Y);
			const double abZ = static_cast<double>(b.m_Z) - static_cast<double>(a.m_Z);
			const double acX = static_cast<double>(c.m_X) - static_cast<double>(a.m_X);
			const double acY = static_cast<double>(c.m_Y) - static_cast<double>(a.m_Y);
			const double acZ = static_cast<double>(c.m_Z) - static_cast<double>(a.m_Z);
			const DensityGradient geometricNormal{
				abY * acZ - abZ * acY,
				abZ * acX - abX * acZ,
				abX * acY - abY * acX,
			};

			// Winding is a topology contract. Density gradients remain suitable for
			// smooth vertex normals, but discontinuous edits can make them disagree with
			// this tetrahedron's actual solid/empty classification.
			const DensityGradient outwardDirection =
				ComputeSolidToEmptyDirection(cubeCorners, tetrahedron);
			if (!IsFinite(outwardDirection))
			{
				return { ValidationError::NonFiniteDensityGradient };
			}
			const double outwardLengthSquared = outwardDirection.m_X * outwardDirection.m_X +
				outwardDirection.m_Y * outwardDirection.m_Y +
				outwardDirection.m_Z * outwardDirection.m_Z;
			if (!std::isfinite(outwardLengthSquared) || outwardLengthSquared <= 0.0)
			{
				return { ValidationError::DegenerateDensityGradient };
			}
			const double inverseOutwardLength = 1.0 / std::sqrt(outwardLengthSquared);
			const Float3 normalizedOutwardDirection{
				static_cast<float>(outwardDirection.m_X * inverseOutwardLength),
				static_cast<float>(outwardDirection.m_Y * inverseOutwardLength),
				static_cast<float>(outwardDirection.m_Z * inverseOutwardLength),
			};
			if (!IsFinite(normalizedOutwardDirection))
			{
				return { ValidationError::NonFiniteDensityGradient };
			}

			const double alignment =
				geometricNormal.m_X * static_cast<double>(normalizedOutwardDirection.m_X) +
				geometricNormal.m_Y * static_cast<double>(normalizedOutwardDirection.m_Y) +
				geometricNormal.m_Z * static_cast<double>(normalizedOutwardDirection.m_Z);
			if (!std::isfinite(alignment) || alignment == 0.0)
			{
				return { ValidationError::InvalidMeshWinding };
			}
			if (alignment < 0.0)
			{
				std::swap(triangle.m_Vertices[1], triangle.m_Vertices[2]);
			}
			triangle.m_WindingEvidence = {
				.m_OutwardDirection = normalizedOutwardDirection,
			};
			return {};
		}

		[[nodiscard]] ValidationResult HasCanonicalDegeneracy(const ReferenceTriangle& triangle,
			const MeshQuantizationContext& quantizationContext, bool& degenerate) noexcept
		{
			std::array<QuantizedMeshPosition, 3> positions{};
			for (std::size_t index = 0; index < positions.size(); ++index)
			{
				const ValidationResult quantizationResult = QuantizeMeshPosition(
					triangle.m_Vertices[index].m_Position, quantizationContext, positions[index]);
				if (quantizationResult.Failed())
				{
					return quantizationResult;
				}
				if (!quantizationContext.ContainsTargetCellDomain(positions[index]))
				{
					return {
						ValidationError::MeshGeometryOutsideTargetCellDomain,
					};
				}
			}

			degenerate = positions[0] == positions[1] || positions[0] == positions[2] ||
				positions[1] == positions[2];
			return {};
		}

		struct PreparedReferenceSampleGrid
		{
			std::size_t m_SampleCountX = 0;
			std::size_t m_SampleCountY = 0;
			std::size_t m_SampleCountZ = 0;
			std::vector<ReferenceEdgeEndpoint> m_Samples;
		};

		struct PreparedVoxelSampleGrid
		{
			SampleAabb m_Bounds{};
			std::size_t m_SampleCountX = 0;
			std::size_t m_SampleCountY = 0;
			std::size_t m_SampleCountZ = 0;
			std::vector<VoxelSample> m_Samples;
		};

		struct PendingMaterialSection
		{
			std::vector<std::uint32_t> m_Indices;
			std::vector<MeshTriangleWindingEvidence> m_WindingEvidence;
		};

		[[nodiscard]] ValidationResult ValidateReferenceMeshCapacity(
			const CellAabb& cellBounds) noexcept
		{
			const std::uint64_t cellCountX = static_cast<std::uint64_t>(
				static_cast<std::int64_t>(cellBounds.m_MaxExclusive.m_X) -
				static_cast<std::int64_t>(cellBounds.m_Min.m_X));
			const std::uint64_t cellCountY = static_cast<std::uint64_t>(
				static_cast<std::int64_t>(cellBounds.m_MaxExclusive.m_Y) -
				static_cast<std::int64_t>(cellBounds.m_Min.m_Y));
			const std::uint64_t cellCountZ = static_cast<std::uint64_t>(
				static_cast<std::int64_t>(cellBounds.m_MaxExclusive.m_Z) -
				static_cast<std::int64_t>(cellBounds.m_Min.m_Z));
			const std::optional<std::uint64_t> cellCountXY = CheckedMul(cellCountX, cellCountY);
			const std::optional<std::uint64_t> cellCount =
				cellCountXY ? CheckedMul(*cellCountXY, cellCountZ) : std::nullopt;
			const std::optional<std::uint64_t> triangleCount =
				cellCount ? CheckedMul(*cellCount, static_cast<std::uint64_t>(12)) : std::nullopt;
			const std::optional<std::uint64_t> vertexCount =
				triangleCount ? CheckedMul(*triangleCount, static_cast<std::uint64_t>(3))
				: std::nullopt;
			if (!cellCount || !triangleCount || !vertexCount)
			{
				return { ValidationError::ArithmeticOverflow };
			}
			if (*vertexCount >
				static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
			{
				return { ValidationError::ArithmeticOverflow };
			}

			if (!CheckedNarrow<std::size_t>(*vertexCount) ||
				!CheckedNarrow<std::size_t>(*triangleCount))
			{
				return { ValidationError::ArithmeticOverflow };
			}
			return {};
		}

		[[nodiscard]] ValidationResult ComputeSampleGridShape(const SampleAabb& bounds,
			std::size_t& sizeX, std::size_t& sizeY, std::size_t& sizeZ,
			std::size_t& capacity) noexcept
		{
			const std::uint64_t countX =
				static_cast<std::uint64_t>(static_cast<std::int64_t>(bounds.m_MaxExclusive.m_X) -
					static_cast<std::int64_t>(bounds.m_Min.m_X));
			const std::uint64_t countY =
				static_cast<std::uint64_t>(static_cast<std::int64_t>(bounds.m_MaxExclusive.m_Y) -
					static_cast<std::int64_t>(bounds.m_Min.m_Y));
			const std::uint64_t countZ =
				static_cast<std::uint64_t>(static_cast<std::int64_t>(bounds.m_MaxExclusive.m_Z) -
					static_cast<std::int64_t>(bounds.m_Min.m_Z));
			const std::optional<std::uint64_t> countXY = CheckedMul(countX, countY);
			const std::optional<std::uint64_t> count =
				countXY ? CheckedMul(*countXY, countZ) : std::nullopt;
			const std::optional<std::size_t> preparedSizeX = CheckedNarrow<std::size_t>(countX);
			const std::optional<std::size_t> preparedSizeY = CheckedNarrow<std::size_t>(countY);
			const std::optional<std::size_t> preparedSizeZ = CheckedNarrow<std::size_t>(countZ);
			const std::optional<std::size_t> preparedCapacity =
				count ? CheckedNarrow<std::size_t>(*count) : std::nullopt;
			if (!preparedSizeX || !preparedSizeY || !preparedSizeZ || !preparedCapacity)
			{
				return { ValidationError::ArithmeticOverflow };
			}

			sizeX = *preparedSizeX;
			sizeY = *preparedSizeY;
			sizeZ = *preparedSizeZ;
			capacity = *preparedCapacity;
			return {};
		}

		[[nodiscard]] std::size_t FlattenPreparedSample(SampleCoord coordinate,
			const SampleAabb& bounds, std::size_t sizeX, std::size_t sizeY) noexcept
		{
			const std::size_t x =
				static_cast<std::size_t>(static_cast<std::int64_t>(coordinate.m_X) -
					static_cast<std::int64_t>(bounds.m_Min.m_X));
			const std::size_t y =
				static_cast<std::size_t>(static_cast<std::int64_t>(coordinate.m_Y) -
					static_cast<std::int64_t>(bounds.m_Min.m_Y));
			const std::size_t z =
				static_cast<std::size_t>(static_cast<std::int64_t>(coordinate.m_Z) -
					static_cast<std::int64_t>(bounds.m_Min.m_Z));
			return x + sizeX * (y + sizeY * z);
		}

		[[nodiscard]] ValidationResult PrepareVoxelSampleGrid(
			const VoxelWorld& world, const SampleAabb& bounds, PreparedVoxelSampleGrid& grid)
		{
			std::size_t sizeX = 0;
			std::size_t sizeY = 0;
			std::size_t sizeZ = 0;
			std::size_t capacity = 0;
			const ValidationResult shapeResult =
				ComputeSampleGridShape(bounds, sizeX, sizeY, sizeZ, capacity);
			if (shapeResult.Failed())
			{
				return shapeResult;
			}

			PreparedVoxelSampleGrid prepared{
				.m_Bounds = bounds,
				.m_SampleCountX = sizeX,
				.m_SampleCountY = sizeY,
				.m_SampleCountZ = sizeZ,
				.m_Samples = std::vector<VoxelSample>(capacity),
			};
			for (std::size_t z = 0; z < sizeZ; ++z)
			{
				for (std::size_t y = 0; y < sizeY; ++y)
				{
					for (std::size_t x = 0; x < sizeX; ++x)
					{
						const SampleCoord coordinate{
							static_cast<std::int32_t>(static_cast<std::int64_t>(bounds.m_Min.m_X) +
													  static_cast<std::int64_t>(x)),
							static_cast<std::int32_t>(static_cast<std::int64_t>(bounds.m_Min.m_Y) +
													  static_cast<std::int64_t>(y)),
							static_cast<std::int32_t>(static_cast<std::int64_t>(bounds.m_Min.m_Z) +
													  static_cast<std::int64_t>(z)),
						};
						VoxelSample sample{};
						const ValidationResult sampleResult =
							world.ReadCurrentSample(coordinate, sample);
						if (sampleResult.Failed())
						{
							return sampleResult;
						}
						const std::size_t index = x + sizeX * (y + sizeY * z);
						prepared.m_Samples[index] = sample;
					}
				}
			}

			grid = std::move(prepared);
			return {};
		}

		[[nodiscard]] ValidationResult ReadPreparedDensity(const PreparedVoxelSampleGrid& grid,
			SampleCoord coordinate, std::uint8_t& density) noexcept
		{
			if (!grid.m_Bounds.Contains(coordinate))
			{
				return {
					ValidationError::SampleOutsideLogicalBounds,
				};
			}
			const std::size_t index = FlattenPreparedSample(
				coordinate, grid.m_Bounds, grid.m_SampleCountX, grid.m_SampleCountY);
			density = grid.m_Samples[index].m_Density;
			return {};
		}

		[[nodiscard]] ValidationResult PrepareReferenceSampleGrid(
			const VoxelWorld& world, const CellAabb& cellBounds, PreparedReferenceSampleGrid& grid)
		{
			const SampleAabb logicalBounds = world.GetLogicalSampleBounds();
			const std::optional<std::int32_t> targetMaximumExclusiveX = CheckedNarrow<std::int32_t>(
				static_cast<std::int64_t>(cellBounds.m_MaxExclusive.m_X) + 1);
			const std::optional<std::int32_t> targetMaximumExclusiveY = CheckedNarrow<std::int32_t>(
				static_cast<std::int64_t>(cellBounds.m_MaxExclusive.m_Y) + 1);
			const std::optional<std::int32_t> targetMaximumExclusiveZ = CheckedNarrow<std::int32_t>(
				static_cast<std::int64_t>(cellBounds.m_MaxExclusive.m_Z) + 1);
			if (!targetMaximumExclusiveX || !targetMaximumExclusiveY || !targetMaximumExclusiveZ)
			{
				return {
					ValidationError::CoordinateOutOfRange,
				};
			}

			const SampleAabb targetBounds{
				.m_Min = {
					cellBounds.m_Min.m_X,
					cellBounds.m_Min.m_Y,
					cellBounds.m_Min.m_Z,
				},
				.m_MaxExclusive = {
					*targetMaximumExclusiveX,
					*targetMaximumExclusiveY,
					*targetMaximumExclusiveZ,
				},
			};
			const SampleAabb expandedBounds{
				.m_Min = {
					static_cast<std::int32_t>(std::max(
						static_cast<std::int64_t>(
							logicalBounds.m_Min.m_X),
						static_cast<std::int64_t>(
							targetBounds.m_Min.m_X) -
							1)),
					static_cast<std::int32_t>(std::max(
						static_cast<std::int64_t>(
							logicalBounds.m_Min.m_Y),
						static_cast<std::int64_t>(
							targetBounds.m_Min.m_Y) -
							1)),
					static_cast<std::int32_t>(std::max(
						static_cast<std::int64_t>(
							logicalBounds.m_Min.m_Z),
						static_cast<std::int64_t>(
							targetBounds.m_Min.m_Z) -
							1)),
				},
				.m_MaxExclusive = {
					static_cast<std::int32_t>(std::min(
						static_cast<std::int64_t>(
							logicalBounds
								.m_MaxExclusive.m_X),
						static_cast<std::int64_t>(
							targetBounds
								.m_MaxExclusive.m_X) +
							1)),
					static_cast<std::int32_t>(std::min(
						static_cast<std::int64_t>(
							logicalBounds
								.m_MaxExclusive.m_Y),
						static_cast<std::int64_t>(
							targetBounds
								.m_MaxExclusive.m_Y) +
							1)),
					static_cast<std::int32_t>(std::min(
						static_cast<std::int64_t>(
							logicalBounds
								.m_MaxExclusive.m_Z),
						static_cast<std::int64_t>(
							targetBounds
								.m_MaxExclusive.m_Z) +
							1)),
				},
			};
			PreparedVoxelSampleGrid voxelSamples;
			const ValidationResult voxelSampleResult =
				PrepareVoxelSampleGrid(world, expandedBounds, voxelSamples);
			if (voxelSampleResult.Failed())
			{
				return voxelSampleResult;
			}

			std::size_t sizeX = 0;
			std::size_t sizeY = 0;
			std::size_t sizeZ = 0;
			std::size_t capacity = 0;
			const ValidationResult shapeResult =
				ComputeSampleGridShape(targetBounds, sizeX, sizeY, sizeZ, capacity);
			if (shapeResult.Failed())
			{
				return shapeResult;
			}
			PreparedReferenceSampleGrid prepared{
				.m_SampleCountX = sizeX,
				.m_SampleCountY = sizeY,
				.m_SampleCountZ = sizeZ,
				.m_Samples = std::vector<ReferenceEdgeEndpoint>(capacity),
			};
			const auto readDensity = [&voxelSamples](
				SampleCoord coordinate, std::uint8_t& density) noexcept
				{ return ReadPreparedDensity(voxelSamples, coordinate, density); };
			for (std::size_t z = 0; z < sizeZ; ++z)
			{
				for (std::size_t y = 0; y < sizeY; ++y)
				{
					for (std::size_t x = 0; x < sizeX; ++x)
					{
						const SampleCoord coordinate{
							static_cast<std::int32_t>(
								static_cast<std::int64_t>(targetBounds.m_Min.m_X) +
								static_cast<std::int64_t>(x)),
							static_cast<std::int32_t>(
								static_cast<std::int64_t>(targetBounds.m_Min.m_Y) +
								static_cast<std::int64_t>(y)),
							static_cast<std::int32_t>(
								static_cast<std::int64_t>(targetBounds.m_Min.m_Z) +
								static_cast<std::int64_t>(z)),
						};
						const std::size_t voxelSampleIndex =
							FlattenPreparedSample(coordinate, voxelSamples.m_Bounds,
								voxelSamples.m_SampleCountX, voxelSamples.m_SampleCountY);
						const VoxelSample sample = voxelSamples.m_Samples[voxelSampleIndex];
						DensityGradient gradient{};
						const ValidationResult xResult =
							ComputeAxisDensityGradient(logicalBounds, coordinate, CoordinateAxis::X,
								sample.m_Density, readDensity, gradient.m_X);
						if (xResult.Failed())
						{
							return xResult;
						}
						const ValidationResult yResult =
							ComputeAxisDensityGradient(logicalBounds, coordinate, CoordinateAxis::Y,
								sample.m_Density, readDensity, gradient.m_Y);
						if (yResult.Failed())
						{
							return yResult;
						}
						const ValidationResult zResult =
							ComputeAxisDensityGradient(logicalBounds, coordinate, CoordinateAxis::Z,
								sample.m_Density, readDensity, gradient.m_Z);
						if (zResult.Failed())
						{
							return zResult;
						}

						const std::size_t index = x + sizeX * (y + sizeY * z);
						prepared.m_Samples[index] = {
							.m_Coordinate = coordinate,
							.m_Sample = sample,
							.m_DensityGradient = gradient,
						};
					}
				}
			}

			grid = std::move(prepared);
			return {};
		}

		[[nodiscard]] const ReferenceEdgeEndpoint& GetPreparedSample(
			const PreparedReferenceSampleGrid& grid, std::size_t cellX, std::size_t cellY,
			std::size_t cellZ, CellCornerOffset corner) noexcept
		{
			const std::size_t x = cellX + corner.m_X;
			const std::size_t y = cellY + corner.m_Y;
			const std::size_t z = cellZ + corner.m_Z;
			const std::size_t index = x + grid.m_SampleCountX * (y + grid.m_SampleCountY * z);
			return grid.m_Samples[index];
		}

		void IncludeBoundsPoint(Float3 point, bool& hasBounds, FloatAabb& bounds) noexcept
		{
			if (!hasBounds)
			{
				bounds = {
					.m_Min = point,
					.m_Max = point,
				};
				hasBounds = true;
				return;
			}
			bounds.m_Min.m_X = std::min(bounds.m_Min.m_X, point.m_X);
			bounds.m_Min.m_Y = std::min(bounds.m_Min.m_Y, point.m_Y);
			bounds.m_Min.m_Z = std::min(bounds.m_Min.m_Z, point.m_Z);
			bounds.m_Max.m_X = std::max(bounds.m_Max.m_X, point.m_X);
			bounds.m_Max.m_Y = std::max(bounds.m_Max.m_Y, point.m_Y);
			bounds.m_Max.m_Z = std::max(bounds.m_Max.m_Z, point.m_Z);
		}

		[[nodiscard]] ValidationResult AppendReferenceTriangle(const ReferenceTriangle& triangle,
			VoxelMaterial material, MeshData& mesh,
			std::map<VoxelMaterial, PendingMaterialSection>& materialSections, bool& hasBounds)
		{
			const std::optional<std::uint32_t> baseIndex =
				CheckedNarrow<std::uint32_t>(mesh.m_Vertices.size());
			const std::optional<std::uint32_t> secondIndex =
				baseIndex ? CheckedAdd(*baseIndex, static_cast<std::uint32_t>(1)) : std::nullopt;
			const std::optional<std::uint32_t> thirdIndex =
				baseIndex ? CheckedAdd(*baseIndex, static_cast<std::uint32_t>(2)) : std::nullopt;
			if (!baseIndex || !secondIndex || !thirdIndex)
			{
				return { ValidationError::ArithmeticOverflow };
			}

			for (const ReferenceEdgeVertex& vertex : triangle.m_Vertices)
			{
				mesh.m_Vertices.push_back({
					.m_Position = vertex.m_Position,
					.m_Normal = vertex.m_Normal,
					});
				IncludeBoundsPoint(vertex.m_Position, hasBounds, mesh.m_Bounds);
			}

			PendingMaterialSection& section = materialSections[material];
			section.m_Indices.push_back(*baseIndex);
			section.m_Indices.push_back(*secondIndex);
			section.m_Indices.push_back(*thirdIndex);
			section.m_WindingEvidence.push_back(triangle.m_WindingEvidence);
			return {};
		}
	}

	ReferenceMesher::ReferenceMesher(const VoxelWorld& world) noexcept : m_World(world)
	{
	}

	ValidationResult ReferenceMesher::ComputeSampleDensityGradient(
		SampleCoord coordinate, DensityGradient& gradient) const noexcept
	{
		const SampleAabb bounds = m_World.GetLogicalSampleBounds();
		if (!bounds.Contains(coordinate))
		{
			return { ValidationError::SampleOutsideLogicalBounds };
		}

		std::uint8_t centerDensity = 0;
		const ValidationResult centerResult = ReadDensity(m_World, coordinate, centerDensity);
		if (centerResult.Failed())
		{
			return centerResult;
		}

		DensityGradient prepared{};
		const auto readDensity = [this](SampleCoord sample, std::uint8_t& density) noexcept
			{ return ReadDensity(m_World, sample, density); };
		const ValidationResult xResult = ComputeAxisDensityGradient(
			bounds, coordinate, CoordinateAxis::X, centerDensity, readDensity, prepared.m_X);
		if (xResult.Failed())
		{
			return xResult;
		}
		const ValidationResult yResult = ComputeAxisDensityGradient(
			bounds, coordinate, CoordinateAxis::Y, centerDensity, readDensity, prepared.m_Y);
		if (yResult.Failed())
		{
			return yResult;
		}
		const ValidationResult zResult = ComputeAxisDensityGradient(
			bounds, coordinate, CoordinateAxis::Z, centerDensity, readDensity, prepared.m_Z);
		if (zResult.Failed())
		{
			return zResult;
		}

		gradient = prepared;
		return {};
	}

	ValidationResult ReferenceMesher::InterpolateEdge(ReferenceEdgeEndpoint first,
		ReferenceEdgeEndpoint second, ChunkCoord chunk, ReferenceEdgeVertex& vertex) const noexcept
	{
		MeshQuantizationContext quantizationContext;
		const ValidationResult contextResult =
			PrepareMeshQuantizationContext(m_World.GetConfig(), chunk, quantizationContext);
		if (contextResult.Failed())
		{
			return contextResult;
		}

		ReferenceEdgeVertex prepared{};
		const ValidationResult interpolationResult =
			InterpolateEdgeCandidate(m_World, chunk, first, second, prepared);
		if (interpolationResult.Failed())
		{
			return interpolationResult;
		}

		Float3 normal{};
		const ValidationResult normalResult =
			ComputeOutwardNormal(prepared.m_DensityGradient, normal);
		if (normalResult.Failed())
		{
			return normalResult;
		}
		QuantizedMeshPosition quantizedPosition{};
		const ValidationResult quantizationResult =
			QuantizeMeshPosition(prepared.m_Position, quantizationContext, quantizedPosition);
		if (quantizationResult.Failed())
		{
			return quantizationResult;
		}
		if (!quantizationContext.ContainsTargetCellDomain(quantizedPosition))
		{
			return {
				ValidationError::MeshGeometryOutsideTargetCellDomain,
			};
		}
		prepared.m_Normal = normal;
		vertex = prepared;
		return {};
	}

	ValidationResult ReferenceMesher::PolygonizeTetrahedron(
		const std::array<ReferenceEdgeEndpoint, 8>& cubeCorners, std::uint8_t tetrahedronIndex,
		const MeshQuantizationContext& quantizationContext,
		ReferenceTetrahedronPolygonization& polygonization) const noexcept
	{
		if (static_cast<std::size_t>(tetrahedronIndex) >= ReferenceFreudenthalTetrahedra.size())
		{
			return { ValidationError::InvalidReferenceTetrahedron };
		}
		if (!quantizationContext.IsPrepared())
		{
			return {
				ValidationError::UnpreparedMeshQuantizationContext,
			};
		}

		const std::array<std::uint8_t, 4>& tetrahedron =
			ReferenceFreudenthalTetrahedra[tetrahedronIndex];
		CellCoord cell{};
		const ValidationResult cornerResult =
			ValidateTetrahedronCorners(m_World, cubeCorners, tetrahedron, cell);
		if (cornerResult.Failed())
		{
			return cornerResult;
		}
		OwnedCellAddress cellAddress{};
		const ValidationResult ownerResult =
			ResolveCellOwner(cell, m_World.GetConfig().m_ChunkCellCount, cellAddress);
		if (ownerResult.Failed())
		{
			return ownerResult;
		}
		if (!quantizationContext.IsCompatible(m_World.GetConfig(), cellAddress.m_Owner))
		{
			return {
				ValidationError::MismatchedMeshQuantizationContext,
			};
		}
		return PolygonizePreparedTetrahedron(cubeCorners, tetrahedronIndex, cellAddress.m_Owner,
			quantizationContext, polygonization);
	}

	ValidationResult ReferenceMesher::PolygonizePreparedTetrahedron(
		const std::array<ReferenceEdgeEndpoint, 8>& cubeCorners, std::uint8_t tetrahedronIndex,
		ChunkCoord chunk, const MeshQuantizationContext& quantizationContext,
		ReferenceTetrahedronPolygonization& polygonization) const noexcept
	{
		const std::array<std::uint8_t, 4>& tetrahedron =
			ReferenceFreudenthalTetrahedra[tetrahedronIndex];
		std::array<std::uint8_t, 4> solidCorners{};
		std::array<std::uint8_t, 4> emptyCorners{};
		std::uint8_t solidCount = 0;
		std::uint8_t emptyCount = 0;
		for (const std::uint8_t cornerId : tetrahedron)
		{
			if (cubeCorners[cornerId].m_Sample.m_Density >= IsoValue)
			{
				solidCorners[solidCount++] = cornerId;
			}
			else
			{
				emptyCorners[emptyCount++] = cornerId;
			}
		}

		ReferenceTetrahedronPolygonization prepared{};
		if (solidCount == 0 || solidCount == 4)
		{
			polygonization = prepared;
			return {};
		}
		prepared.m_Material = SelectTetrahedronMaterial(cubeCorners, tetrahedron);

		std::array<ReferenceTriangle, 2> candidates{};
		std::uint8_t candidateCount = 0;
		if (solidCount == 1 || solidCount == 3)
		{
			std::array<ReferenceEdgeVertex, 3> crossingVertices{};
			std::uint8_t crossingCount = 0;
			for (const std::array<std::uint8_t, 2>& edge : ReferenceTetrahedronEdges)
			{
				const std::uint8_t firstCornerId = tetrahedron[edge[0]];
				const std::uint8_t secondCornerId = tetrahedron[edge[1]];
				const bool firstSolid = cubeCorners[firstCornerId].m_Sample.m_Density >= IsoValue;
				const bool secondSolid = cubeCorners[secondCornerId].m_Sample.m_Density >= IsoValue;
				if (firstSolid == secondSolid)
				{
					continue;
				}
				if (static_cast<std::size_t>(crossingCount) >= crossingVertices.size())
				{
					return {
						ValidationError::InvalidReferenceTetrahedron,
					};
				}

				const ValidationResult interpolationResult = InterpolatePreparedEdgeCandidate(
					m_World.GetConfig(), chunk, cubeCorners[firstCornerId],
					cubeCorners[secondCornerId], crossingVertices[crossingCount]);
				if (interpolationResult.Failed())
				{
					return interpolationResult;
				}
				++crossingCount;
			}
			if (static_cast<std::size_t>(crossingCount) != crossingVertices.size())
			{
				return {
					ValidationError::InvalidReferenceTetrahedron,
				};
			}

			candidates[0].m_Vertices = crossingVertices;
			candidateCount = 1;
		}
		else
		{
			std::array<std::uint8_t, 2> sortedSolid{
				solidCorners[0],
				solidCorners[1],
			};
			std::array<std::uint8_t, 2> sortedEmpty{
				emptyCorners[0],
				emptyCorners[1],
			};
			std::sort(sortedSolid.begin(), sortedSolid.end());
			std::sort(sortedEmpty.begin(), sortedEmpty.end());

			const std::uint8_t i = sortedSolid[0];
			const std::uint8_t j = sortedSolid[1];
			const std::uint8_t k = sortedEmpty[0];
			const std::uint8_t l = sortedEmpty[1];
			std::array<ReferenceEdgeVertex, 4> perimeter{};
			const std::array<std::array<std::uint8_t, 2>, 4> perimeterEdges{
				std::array<std::uint8_t, 2>{ i, k },
				std::array<std::uint8_t, 2>{ i, l },
				std::array<std::uint8_t, 2>{ j, l },
				std::array<std::uint8_t, 2>{ j, k },
			};
			for (std::size_t edgeIndex = 0; edgeIndex < perimeterEdges.size(); ++edgeIndex)
			{
				const ValidationResult interpolationResult = InterpolatePreparedEdgeCandidate(
					m_World.GetConfig(), chunk, cubeCorners[perimeterEdges[edgeIndex][0]],
					cubeCorners[perimeterEdges[edgeIndex][1]], perimeter[edgeIndex]);
				if (interpolationResult.Failed())
				{
					return interpolationResult;
				}
			}

			candidates[0].m_Vertices = {
				perimeter[0],
				perimeter[1],
				perimeter[2],
			};
			candidates[1].m_Vertices = {
				perimeter[0],
				perimeter[2],
				perimeter[3],
			};
			candidateCount = 2;
		}

		for (std::uint8_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
		{
			ReferenceTriangle candidate = candidates[candidateIndex];
			bool degenerate = false;
			const ValidationResult canonicalResult =
				HasCanonicalDegeneracy(candidate, quantizationContext, degenerate);
			if (canonicalResult.Failed())
			{
				return canonicalResult;
			}
			if (degenerate)
			{
				++prepared.m_SkippedDegenerateTriangleCount;
				continue;
			}

			const ValidationResult areaResult = ValidateMeshTriangleArea(
				candidate.m_Vertices[0].m_Position, candidate.m_Vertices[1].m_Position,
				candidate.m_Vertices[2].m_Position, m_World.GetConfig().m_VoxelSize);
			if (areaResult.Failed())
			{
				return areaResult;
			}

			for (ReferenceEdgeVertex& vertex : candidate.m_Vertices)
			{
				const ValidationResult normalResult =
					ComputeOutwardNormal(vertex.m_DensityGradient, vertex.m_Normal);
				if (normalResult.Failed())
				{
					return normalResult;
				}
			}

			const ValidationResult windingResult =
				OrientReferenceTriangle(cubeCorners, tetrahedron, candidate);
			if (windingResult.Failed())
			{
				return windingResult;
			}

			prepared.m_Triangles[prepared.m_TriangleCount++] = std::move(candidate);
		}
		if (prepared.m_TriangleCount == 0)
		{
			prepared.m_Material = VoxelMaterial::Empty;
		}

		polygonization = std::move(prepared);
		return {};
	}

	ValidationResult ReferenceMesher::MeshChunk(ChunkCoord chunk, ChunkMeshRecord& record) const
	{
		const VoxelWorldConfig& config = m_World.GetConfig();
		CellAabb cellBounds{};
		const ValidationResult intersectionResult = IntersectCellOwnerChunk(
			chunk, config.m_ChunkCellCount, config.m_LogicalCellBounds, cellBounds);
		if (intersectionResult.Failed())
		{
			return intersectionResult;
		}

		MeshQuantizationContext quantizationContext;
		const ValidationResult contextResult =
			PrepareMeshQuantizationContext(config, chunk, quantizationContext);
		if (contextResult.Failed())
		{
			return contextResult;
		}

		const ValidationResult capacityResult = ValidateReferenceMeshCapacity(cellBounds);
		if (capacityResult.Failed())
		{
			return capacityResult;
		}

		PreparedReferenceSampleGrid sampleGrid;
		const ValidationResult gridResult =
			PrepareReferenceSampleGrid(m_World, cellBounds, sampleGrid);
		if (gridResult.Failed())
		{
			return gridResult;
		}

		MeshData mesh;
		std::map<VoxelMaterial, PendingMaterialSection> materialSections;
		bool hasBounds = false;
		std::uint64_t skippedDegenerateTriangleCount = 0;
		ChunkBoundaryContourSet boundaryContours = MakeEmptyChunkBoundaryContourSet();
		const std::size_t cellCountX = sampleGrid.m_SampleCountX - 1;
		const std::size_t cellCountY = sampleGrid.m_SampleCountY - 1;
		const std::size_t cellCountZ = sampleGrid.m_SampleCountZ - 1;
		for (std::size_t cellZ = 0; cellZ < cellCountZ; ++cellZ)
		{
			for (std::size_t cellY = 0; cellY < cellCountY; ++cellY)
			{
				for (std::size_t cellX = 0; cellX < cellCountX; ++cellX)
				{
					std::array<ReferenceEdgeEndpoint, 8> cubeCorners{};
					for (std::size_t cornerIndex = 0; cornerIndex < cubeCorners.size();
						++cornerIndex)
					{
						cubeCorners[cornerIndex] = GetPreparedSample(sampleGrid, cellX, cellY,
							cellZ, ReferenceCubeCornerOffsets[cornerIndex]);
					}

					const ValidationResult contourResult =
						AppendCellBoundaryContours(config, chunk, cubeCorners, boundaryContours);
					if (contourResult.Failed())
					{
						return contourResult;
					}

					for (std::uint8_t tetrahedronIndex = 0;
						static_cast<std::size_t>(tetrahedronIndex) <
						ReferenceFreudenthalTetrahedra.size();
						++tetrahedronIndex)
					{
						ReferenceTetrahedronPolygonization polygonization{};
						const ValidationResult polygonizationResult =
							PolygonizePreparedTetrahedron(cubeCorners, tetrahedronIndex, chunk,
								quantizationContext, polygonization);
						if (polygonizationResult.Failed())
						{
							return polygonizationResult;
						}

						const std::optional<std::uint64_t> nextSkippedCount =
							CheckedAdd(skippedDegenerateTriangleCount,
								static_cast<std::uint64_t>(
									polygonization.m_SkippedDegenerateTriangleCount));
						if (!nextSkippedCount)
						{
							return {
								ValidationError::ArithmeticOverflow,
							};
						}
						skippedDegenerateTriangleCount = *nextSkippedCount;

						for (std::uint8_t triangleIndex = 0;
							triangleIndex < polygonization.m_TriangleCount; ++triangleIndex)
						{
							const ValidationResult appendResult =
								AppendReferenceTriangle(polygonization.m_Triangles[triangleIndex],
									polygonization.m_Material, mesh, materialSections, hasBounds);
							if (appendResult.Failed())
							{
								return appendResult;
							}
						}
					}
				}
			}
		}

		for (BoundaryContourRecord& contour : boundaryContours)
		{
			std::sort(
				contour.m_Segments.begin(), contour.m_Segments.end(), BoundaryContourSegmentLess{});
		}
		const ValidationResult contourValidationResult =
			ValidateChunkBoundaryContourSet(boundaryContours, chunk, config);
		if (contourValidationResult.Failed())
		{
			return contourValidationResult;
		}

		std::vector<MeshTriangleWindingEvidence> windingEvidence;
		mesh.m_Sections.reserve(materialSections.size());
		for (auto& [material, pendingSection] : materialSections)
		{
			if (pendingSection.m_Indices.empty())
			{
				continue;
			}
			mesh.m_Sections.push_back({
				.m_Material = material,
				.m_Indices = std::move(pendingSection.m_Indices),
				});
			for (const MeshTriangleWindingEvidence evidence : pendingSection.m_WindingEvidence)
			{
				windingEvidence.push_back(evidence);
			}
		}

		MeshValidationResult validation{};
		const ValidationResult validationResult =
			ValidateAndHashChunkMesh(mesh, windingEvidence, config, chunk, validation);
		if (validationResult.Failed())
		{
			return validationResult;
		}

		ChunkMeshRecord prepared{
			.m_Chunk = chunk,
			.m_SourceWorldVoxelRevision = m_World.GetWorldVoxelRevision(),
			.m_Mesh = std::move(mesh),
			.m_WindingEvidence = std::move(windingEvidence),
			.m_Validation = validation,
			.m_SkippedDegenerateTriangleCount = skippedDegenerateTriangleCount,
			.m_BoundaryContours = std::move(boundaryContours),
		};
		record = std::move(prepared);
		return {};
	}

	ValidationResult ReferenceMesher::MeshWorld(ReferenceWorldMeshingResult& result) const
	{
		const LogicalDomainMetrics& metrics = m_World.GetLogicalDomainMetrics();
		const std::optional<std::size_t> chunkCount =
			CheckedNarrow<std::size_t>(metrics.m_CellOwnerChunkCount);
		if (!chunkCount)
		{
			return {
				ValidationError::LogicalDomainSizeOverflow,
			};
		}

		ReferenceWorldMeshingResult prepared;
		prepared.m_Chunks.reserve(*chunkCount);
		const ChunkAabb& chunkBounds = metrics.m_CellOwnerChunkBounds;
		for (std::int64_t z = chunkBounds.m_Min.m_Z; z < chunkBounds.m_MaxExclusive.m_Z; ++z)
		{
			for (std::int64_t y = chunkBounds.m_Min.m_Y; y < chunkBounds.m_MaxExclusive.m_Y; ++y)
			{
				for (std::int64_t x = chunkBounds.m_Min.m_X; x < chunkBounds.m_MaxExclusive.m_X;
					++x)
				{
					ChunkMeshRecord record;
					const ValidationResult meshResult = MeshChunk(
						{
							static_cast<std::int32_t>(x),
							static_cast<std::int32_t>(y),
							static_cast<std::int32_t>(z),
						},
						record);
					if (meshResult.Failed())
					{
						return meshResult;
					}
					prepared.m_Chunks.push_back(std::move(record));
				}
			}
		}

		const ValidationResult validationResult = ValidateAndHashWorldMeshRecords(
			prepared.m_Chunks, m_World.GetConfig(), prepared.m_Validation);
		if (validationResult.Failed())
		{
			return validationResult;
		}
		const ValidationResult boundaryValidationResult = ValidateBoundaryContourSet(
			prepared.m_Chunks, m_World.GetConfig(), prepared.m_BoundaryValidation);
		if (boundaryValidationResult.Failed())
		{
			return boundaryValidationResult;
		}

		result = std::move(prepared);
		return {};
	}
}
