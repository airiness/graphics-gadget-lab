#include "NapaVoxelCore/Meshing/ReferenceMesher.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

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

		[[nodiscard]] bool IsFinite(
			DensityGradient gradient) noexcept
		{
			return
				std::isfinite(gradient.m_X) &&
				std::isfinite(gradient.m_Y) &&
				std::isfinite(gradient.m_Z);
		}

		[[nodiscard]] bool IsFinite(Float3 value) noexcept
		{
			return
				std::isfinite(value.m_X) &&
				std::isfinite(value.m_Y) &&
				std::isfinite(value.m_Z);
		}

		[[nodiscard]] std::int32_t GetAxis(
			SampleCoord coordinate,
			CoordinateAxis axis) noexcept
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

		void SetAxis(
			SampleCoord& coordinate,
			CoordinateAxis axis,
			std::int32_t value) noexcept
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
			const VoxelWorld& world,
			SampleCoord coordinate,
			std::uint8_t& density) noexcept
		{
			VoxelSample sample{};
			const ValidationResult result =
				world.ReadCurrentSample(coordinate, sample);
			if (result.Failed())
			{
				return result;
			}
			density = sample.m_Density;
			return {};
		}

		[[nodiscard]] ValidationResult ComputeAxisDensityGradient(
			const VoxelWorld& world,
			const SampleAabb& bounds,
			SampleCoord coordinate,
			CoordinateAxis axis,
			std::uint8_t centerDensity,
			double& gradient) noexcept
		{
			const std::int32_t value = GetAxis(coordinate, axis);
			const std::int32_t minimum = GetAxis(bounds.m_Min, axis);
			const std::int32_t maximumExclusive =
				GetAxis(bounds.m_MaxExclusive, axis);

			if (value == minimum)
			{
				SampleCoord positive = coordinate;
				SetAxis(positive, axis, value + 1);
				std::uint8_t positiveDensity = 0;
				const ValidationResult positiveResult =
					ReadDensity(world, positive, positiveDensity);
				if (positiveResult.Failed())
				{
					return positiveResult;
				}
				gradient =
					static_cast<double>(positiveDensity) -
					static_cast<double>(centerDensity);
				return {};
			}

			if (value == maximumExclusive - 1)
			{
				SampleCoord negative = coordinate;
				SetAxis(negative, axis, value - 1);
				std::uint8_t negativeDensity = 0;
				const ValidationResult negativeResult =
					ReadDensity(world, negative, negativeDensity);
				if (negativeResult.Failed())
				{
					return negativeResult;
				}
				gradient =
					static_cast<double>(centerDensity) -
					static_cast<double>(negativeDensity);
				return {};
			}

			SampleCoord negative = coordinate;
			SampleCoord positive = coordinate;
			SetAxis(negative, axis, value - 1);
			SetAxis(positive, axis, value + 1);
			std::uint8_t negativeDensity = 0;
			std::uint8_t positiveDensity = 0;
			const ValidationResult negativeResult =
				ReadDensity(world, negative, negativeDensity);
			if (negativeResult.Failed())
			{
				return negativeResult;
			}
			const ValidationResult positiveResult =
				ReadDensity(world, positive, positiveDensity);
			if (positiveResult.Failed())
			{
				return positiveResult;
			}
			gradient =
				static_cast<double>(positiveDensity) -
				static_cast<double>(negativeDensity);
			return {};
		}

		[[nodiscard]] std::int64_t AbsoluteDifference(
			std::int32_t lhs,
			std::int32_t rhs) noexcept
		{
			const std::int64_t difference =
				static_cast<std::int64_t>(lhs) -
				static_cast<std::int64_t>(rhs);
			return difference < 0 ? -difference : difference;
		}

		[[nodiscard]] bool IsReferenceEdge(
			SampleCoord first,
			SampleCoord second) noexcept
		{
			const std::int64_t x =
				AbsoluteDifference(first.m_X, second.m_X);
			const std::int64_t y =
				AbsoluteDifference(first.m_Y, second.m_Y);
			const std::int64_t z =
				AbsoluteDifference(first.m_Z, second.m_Z);
			return
				x <= 1 &&
				y <= 1 &&
				z <= 1 &&
				x + y + z > 0;
		}

		[[nodiscard]] Float3 InterpolatePosition(
			SampleCoord first,
			SampleCoord second,
			double interpolationT,
			double voxelSize) noexcept
		{
			const double x =
				(static_cast<double>(first.m_X) +
					(static_cast<double>(second.m_X) -
						static_cast<double>(first.m_X)) *
					interpolationT) *
				voxelSize;
			const double y =
				(static_cast<double>(first.m_Y) +
					(static_cast<double>(second.m_Y) -
						static_cast<double>(first.m_Y)) *
					interpolationT) *
				voxelSize;
			const double z =
				(static_cast<double>(first.m_Z) +
					(static_cast<double>(second.m_Z) -
						static_cast<double>(first.m_Z)) *
					interpolationT) *
				voxelSize;
			return {
				static_cast<float>(x),
				static_cast<float>(y),
				static_cast<float>(z),
			};
		}

		[[nodiscard]] DensityGradient InterpolateDensityGradient(
			DensityGradient first,
			DensityGradient second,
			double interpolationT) noexcept
		{
			return {
				first.m_X +
					(second.m_X - first.m_X) * interpolationT,
				first.m_Y +
					(second.m_Y - first.m_Y) * interpolationT,
				first.m_Z +
					(second.m_Z - first.m_Z) * interpolationT,
			};
		}

		[[nodiscard]] ValidationResult ComputeOutwardNormal(
			DensityGradient gradient,
			Float3& normal) noexcept
		{
			if (!IsFinite(gradient))
			{
				return { ValidationError::NonFiniteDensityGradient };
			}

			const double lengthSquared =
				gradient.m_X * gradient.m_X +
				gradient.m_Y * gradient.m_Y +
				gradient.m_Z * gradient.m_Z;
			if (!std::isfinite(lengthSquared))
			{
				return { ValidationError::NonFiniteDensityGradient };
			}
			if (lengthSquared <= 0.0)
			{
				return { ValidationError::DegenerateDensityGradient };
			}

			const double inverseLength =
				1.0 / std::sqrt(lengthSquared);
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

		[[nodiscard]] ValidationResult ValidateTetrahedronCorners(
			const VoxelWorld& world,
			const std::array<ReferenceEdgeEndpoint, 8>& cubeCorners,
			const std::array<std::uint8_t, 4>& tetrahedron) noexcept
		{
			const std::uint8_t firstCornerId = tetrahedron[0];
			const ReferenceEdgeEndpoint& first =
				cubeCorners[firstCornerId];
			const CellCornerOffset firstOffset =
				ReferenceCubeCornerOffsets[firstCornerId];
			const std::int64_t cellX =
				static_cast<std::int64_t>(
					first.m_Coordinate.m_X) -
				static_cast<std::int64_t>(firstOffset.m_X);
			const std::int64_t cellY =
				static_cast<std::int64_t>(
					first.m_Coordinate.m_Y) -
				static_cast<std::int64_t>(firstOffset.m_Y);
			const std::int64_t cellZ =
				static_cast<std::int64_t>(
					first.m_Coordinate.m_Z) -
				static_cast<std::int64_t>(firstOffset.m_Z);
			const SampleAabb bounds = world.GetLogicalSampleBounds();

			for (const std::uint8_t cornerId : tetrahedron)
			{
				const ReferenceEdgeEndpoint& corner =
					cubeCorners[cornerId];
				const CellCornerOffset offset =
					ReferenceCubeCornerOffsets[cornerId];
				if (static_cast<std::int64_t>(
						corner.m_Coordinate.m_X) !=
						cellX +
							static_cast<std::int64_t>(
								offset.m_X) ||
					static_cast<std::int64_t>(
						corner.m_Coordinate.m_Y) !=
						cellY +
							static_cast<std::int64_t>(
								offset.m_Y) ||
					static_cast<std::int64_t>(
						corner.m_Coordinate.m_Z) !=
						cellZ +
							static_cast<std::int64_t>(
								offset.m_Z) ||
					!bounds.Contains(corner.m_Coordinate))
				{
					return {
						ValidationError::InvalidReferenceTetrahedron,
					};
				}

				const ValidationResult sampleResult =
					ValidateVoxelSample(corner.m_Sample);
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
				const VoxelSample sample =
					cubeCorners[cornerId].m_Sample;
				if (sample.m_Density < IsoValue)
				{
					continue;
				}
				if (!selected ||
					sample.m_Density > selectedDensity ||
					(sample.m_Density == selectedDensity &&
						cornerId < selectedCornerId))
				{
					selectedCornerId = cornerId;
					selectedDensity = sample.m_Density;
					selected = true;
				}
			}
			return selected
				? cubeCorners[selectedCornerId].m_Sample.m_Material
				: VoxelMaterial::Empty;
		}

		[[nodiscard]] DensityGradient
			ComputeSolidToEmptyDirection(
				const std::array<ReferenceEdgeEndpoint, 8>&
					cubeCorners,
				const std::array<std::uint8_t, 4>& tetrahedron)
				noexcept
		{
			DensityGradient solidSum{};
			DensityGradient emptySum{};
			double solidCount = 0.0;
			double emptyCount = 0.0;
			for (const std::uint8_t cornerId : tetrahedron)
			{
				const ReferenceEdgeEndpoint& corner =
					cubeCorners[cornerId];
				DensityGradient* const sum =
					corner.m_Sample.m_Density >= IsoValue
						? &solidSum
						: &emptySum;
				double* const count =
					corner.m_Sample.m_Density >= IsoValue
						? &solidCount
						: &emptyCount;
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
				emptySum.m_X / emptyCount -
					solidSum.m_X / solidCount,
				emptySum.m_Y / emptyCount -
					solidSum.m_Y / solidCount,
				emptySum.m_Z / emptyCount -
					solidSum.m_Z / solidCount,
			};
		}

		[[nodiscard]] ValidationResult OrientReferenceTriangle(
			const std::array<ReferenceEdgeEndpoint, 8>& cubeCorners,
			const std::array<std::uint8_t, 4>& tetrahedron,
			ReferenceTriangle& triangle) noexcept
		{
			const Float3 a = triangle.m_Vertices[0].m_Position;
			const Float3 b = triangle.m_Vertices[1].m_Position;
			const Float3 c = triangle.m_Vertices[2].m_Position;
			const double abX =
				static_cast<double>(b.m_X) -
				static_cast<double>(a.m_X);
			const double abY =
				static_cast<double>(b.m_Y) -
				static_cast<double>(a.m_Y);
			const double abZ =
				static_cast<double>(b.m_Z) -
				static_cast<double>(a.m_Z);
			const double acX =
				static_cast<double>(c.m_X) -
				static_cast<double>(a.m_X);
			const double acY =
				static_cast<double>(c.m_Y) -
				static_cast<double>(a.m_Y);
			const double acZ =
				static_cast<double>(c.m_Z) -
				static_cast<double>(a.m_Z);
			const DensityGradient geometricNormal{
				abY * acZ - abZ * acY,
				abZ * acX - abX * acZ,
				abX * acY - abY * acX,
			};

			DensityGradient triangleGradient{};
			for (const ReferenceEdgeVertex& vertex :
				triangle.m_Vertices)
			{
				triangleGradient.m_X +=
					vertex.m_DensityGradient.m_X;
				triangleGradient.m_Y +=
					vertex.m_DensityGradient.m_Y;
				triangleGradient.m_Z +=
					vertex.m_DensityGradient.m_Z;
			}
			if (!IsFinite(triangleGradient))
			{
				return { ValidationError::NonFiniteDensityGradient };
			}

			const double gradientLengthSquared =
				triangleGradient.m_X * triangleGradient.m_X +
				triangleGradient.m_Y * triangleGradient.m_Y +
				triangleGradient.m_Z * triangleGradient.m_Z;
			if (!std::isfinite(gradientLengthSquared))
			{
				return { ValidationError::NonFiniteDensityGradient };
			}
			DensityGradient outwardDirection{};
			if (gradientLengthSquared > 0.0)
			{
				outwardDirection = {
					-triangleGradient.m_X,
					-triangleGradient.m_Y,
					-triangleGradient.m_Z,
				};
			}
			else
			{
				outwardDirection =
					ComputeSolidToEmptyDirection(
						cubeCorners,
						tetrahedron);
			}

			if (!IsFinite(outwardDirection))
			{
				return { ValidationError::NonFiniteDensityGradient };
			}
			const double outwardLengthSquared =
				outwardDirection.m_X * outwardDirection.m_X +
				outwardDirection.m_Y * outwardDirection.m_Y +
				outwardDirection.m_Z * outwardDirection.m_Z;
			if (!std::isfinite(outwardLengthSquared) ||
				outwardLengthSquared <= 0.0)
			{
				return { ValidationError::DegenerateDensityGradient };
			}

			const double alignment =
				geometricNormal.m_X * outwardDirection.m_X +
				geometricNormal.m_Y * outwardDirection.m_Y +
				geometricNormal.m_Z * outwardDirection.m_Z;
			if (!std::isfinite(alignment) || alignment == 0.0)
			{
				return { ValidationError::InvalidMeshWinding };
			}
			if (alignment < 0.0)
			{
				std::swap(
					triangle.m_Vertices[1],
					triangle.m_Vertices[2]);
			}
			return {};
		}

		[[nodiscard]] ValidationResult HasCanonicalDegeneracy(
			const ReferenceTriangle& triangle,
			const MeshQuantizationContext& quantizationContext,
			bool& degenerate) noexcept
		{
			std::array<QuantizedMeshPosition, 3> positions{};
			for (std::size_t index = 0;
				index < positions.size();
				++index)
			{
				const ValidationResult quantizationResult =
					QuantizeMeshPosition(
						triangle.m_Vertices[index].m_Position,
						quantizationContext,
						positions[index]);
				if (quantizationResult.Failed())
				{
					return quantizationResult;
				}
				if (!quantizationContext.ContainsTargetCellDomain(
					positions[index]))
				{
					return {
						ValidationError::
							MeshGeometryOutsideTargetCellDomain,
					};
				}
			}

			degenerate =
				positions[0] == positions[1] ||
				positions[0] == positions[2] ||
				positions[1] == positions[2];
			return {};
		}
	}

	ReferenceMesher::ReferenceMesher(
		const VoxelWorld& world) noexcept
		: m_World(world)
	{
	}

	ValidationResult ReferenceMesher::ComputeSampleDensityGradient(
		SampleCoord coordinate,
		DensityGradient& gradient) const noexcept
	{
		const SampleAabb bounds = m_World.GetLogicalSampleBounds();
		if (!bounds.Contains(coordinate))
		{
			return { ValidationError::SampleOutsideLogicalBounds };
		}

		std::uint8_t centerDensity = 0;
		const ValidationResult centerResult =
			ReadDensity(m_World, coordinate, centerDensity);
		if (centerResult.Failed())
		{
			return centerResult;
		}

		DensityGradient prepared{};
		const ValidationResult xResult =
			ComputeAxisDensityGradient(
				m_World,
				bounds,
				coordinate,
				CoordinateAxis::X,
				centerDensity,
				prepared.m_X);
		if (xResult.Failed())
		{
			return xResult;
		}
		const ValidationResult yResult =
			ComputeAxisDensityGradient(
				m_World,
				bounds,
				coordinate,
				CoordinateAxis::Y,
				centerDensity,
				prepared.m_Y);
		if (yResult.Failed())
		{
			return yResult;
		}
		const ValidationResult zResult =
			ComputeAxisDensityGradient(
				m_World,
				bounds,
				coordinate,
				CoordinateAxis::Z,
				centerDensity,
				prepared.m_Z);
		if (zResult.Failed())
		{
			return zResult;
		}

		gradient = prepared;
		return {};
	}

	ValidationResult ReferenceMesher::InterpolateEdge(
		ReferenceEdgeEndpoint first,
		ReferenceEdgeEndpoint second,
		ReferenceEdgeVertex& vertex) const noexcept
	{
		const SampleAabb bounds = m_World.GetLogicalSampleBounds();
		if (!bounds.Contains(first.m_Coordinate) ||
			!bounds.Contains(second.m_Coordinate))
		{
			return { ValidationError::SampleOutsideLogicalBounds };
		}
		if (!IsReferenceEdge(
			first.m_Coordinate,
			second.m_Coordinate))
		{
			return { ValidationError::InvalidReferenceEdge };
		}

		const ValidationResult firstSampleResult =
			ValidateVoxelSample(first.m_Sample);
		if (firstSampleResult.Failed())
		{
			return firstSampleResult;
		}
		const ValidationResult secondSampleResult =
			ValidateVoxelSample(second.m_Sample);
		if (secondSampleResult.Failed())
		{
			return secondSampleResult;
		}
		if (!IsFinite(first.m_DensityGradient) ||
			!IsFinite(second.m_DensityGradient))
		{
			return { ValidationError::NonFiniteDensityGradient };
		}

		if (SampleCoordZYXLess{}(
			second.m_Coordinate,
			first.m_Coordinate))
		{
			std::swap(first, second);
		}

		const std::int32_t densityA = first.m_Sample.m_Density;
		const std::int32_t densityB = second.m_Sample.m_Density;
		if (densityA == densityB)
		{
			return { ValidationError::EqualDensityReferenceEdge };
		}

		const bool solidA = densityA >= IsoValue;
		const bool solidB = densityB >= IsoValue;
		if (solidA == solidB)
		{
			return { ValidationError::NonCrossingReferenceEdge };
		}

		double interpolationT =
			(static_cast<double>(IsoValue) -
				static_cast<double>(densityA)) /
			(static_cast<double>(densityB) -
				static_cast<double>(densityA));
		interpolationT = std::clamp(interpolationT, 0.0, 1.0);
		if (interpolationT == 0.0)
		{
			interpolationT = 0.0;
		}

		const Float3 position = InterpolatePosition(
			first.m_Coordinate,
			second.m_Coordinate,
			interpolationT,
			static_cast<double>(
				m_World.GetConfig().m_VoxelSize));
		if (!IsFinite(position))
		{
			return { ValidationError::NonFiniteMeshVertex };
		}

		const DensityGradient densityGradient =
			InterpolateDensityGradient(
				first.m_DensityGradient,
				second.m_DensityGradient,
				interpolationT);
		Float3 normal{};
		const ValidationResult normalResult =
			ComputeOutwardNormal(densityGradient, normal);
		if (normalResult.Failed())
		{
			return normalResult;
		}

		vertex = {
			.m_Position = position,
			.m_Normal = normal,
			.m_DensityGradient = densityGradient,
			.m_EndpointA = first.m_Coordinate,
			.m_EndpointB = second.m_Coordinate,
			.m_InterpolationT = interpolationT,
		};
		return {};
	}

	ValidationResult ReferenceMesher::PolygonizeTetrahedron(
		const std::array<ReferenceEdgeEndpoint, 8>& cubeCorners,
		std::uint8_t tetrahedronIndex,
		const MeshQuantizationContext& quantizationContext,
		ReferenceTetrahedronPolygonization& polygonization)
		const noexcept
	{
		if (static_cast<std::size_t>(tetrahedronIndex) >=
			ReferenceFreudenthalTetrahedra.size())
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
		const ValidationResult cornerResult =
			ValidateTetrahedronCorners(
				m_World,
				cubeCorners,
				tetrahedron);
		if (cornerResult.Failed())
		{
			return cornerResult;
		}

		std::array<std::uint8_t, 4> solidCorners{};
		std::array<std::uint8_t, 4> emptyCorners{};
		std::uint8_t solidCount = 0;
		std::uint8_t emptyCount = 0;
		for (const std::uint8_t cornerId : tetrahedron)
		{
			if (cubeCorners[cornerId].m_Sample.m_Density >=
				IsoValue)
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
		prepared.m_Material =
			SelectTetrahedronMaterial(cubeCorners, tetrahedron);

		std::array<ReferenceTriangle, 2> candidates{};
		std::uint8_t candidateCount = 0;
		if (solidCount == 1 || solidCount == 3)
		{
			std::array<ReferenceEdgeVertex, 3>
				crossingVertices{};
			std::uint8_t crossingCount = 0;
			for (const std::array<std::uint8_t, 2>& edge :
				ReferenceTetrahedronEdges)
			{
				const std::uint8_t firstCornerId =
					tetrahedron[edge[0]];
				const std::uint8_t secondCornerId =
					tetrahedron[edge[1]];
				const bool firstSolid =
					cubeCorners[firstCornerId]
						.m_Sample.m_Density >= IsoValue;
				const bool secondSolid =
					cubeCorners[secondCornerId]
						.m_Sample.m_Density >= IsoValue;
				if (firstSolid == secondSolid)
				{
					continue;
				}
				if (static_cast<std::size_t>(crossingCount) >=
					crossingVertices.size())
				{
					return {
						ValidationError::
							InvalidReferenceTetrahedron,
					};
				}

				const ValidationResult interpolationResult =
					InterpolateEdge(
						cubeCorners[firstCornerId],
						cubeCorners[secondCornerId],
						crossingVertices[crossingCount]);
				if (interpolationResult.Failed())
				{
					return interpolationResult;
				}
				++crossingCount;
			}
			if (static_cast<std::size_t>(crossingCount) !=
				crossingVertices.size())
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
			const std::array<std::array<std::uint8_t, 2>, 4>
				perimeterEdges{
					std::array<std::uint8_t, 2>{ i, k },
					std::array<std::uint8_t, 2>{ i, l },
					std::array<std::uint8_t, 2>{ j, l },
					std::array<std::uint8_t, 2>{ j, k },
				};
			for (std::size_t edgeIndex = 0;
				edgeIndex < perimeterEdges.size();
				++edgeIndex)
			{
				const ValidationResult interpolationResult =
					InterpolateEdge(
						cubeCorners[
							perimeterEdges[edgeIndex][0]],
						cubeCorners[
							perimeterEdges[edgeIndex][1]],
						perimeter[edgeIndex]);
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

		for (std::uint8_t candidateIndex = 0;
			candidateIndex < candidateCount;
			++candidateIndex)
		{
			ReferenceTriangle candidate =
				candidates[candidateIndex];
			bool degenerate = false;
			const ValidationResult canonicalResult =
				HasCanonicalDegeneracy(
					candidate,
					quantizationContext,
					degenerate);
			if (canonicalResult.Failed())
			{
				return canonicalResult;
			}
			if (degenerate)
			{
				++prepared.m_SkippedDegenerateTriangleCount;
				continue;
			}

			const ValidationResult areaResult =
				ValidateMeshTriangleArea(
					candidate.m_Vertices[0].m_Position,
					candidate.m_Vertices[1].m_Position,
					candidate.m_Vertices[2].m_Position,
					m_World.GetConfig().m_VoxelSize);
			if (areaResult.Failed())
			{
				return areaResult;
			}

			const ValidationResult windingResult =
				OrientReferenceTriangle(
					cubeCorners,
					tetrahedron,
					candidate);
			if (windingResult.Failed())
			{
				return windingResult;
			}

			prepared.m_Triangles[
				prepared.m_TriangleCount++] =
				std::move(candidate);
		}

		polygonization = std::move(prepared);
		return {};
	}
}
