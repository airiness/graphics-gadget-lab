#include "NapaVoxelCore/Meshing/ReferenceMesher.h"

#include <algorithm>
#include <cmath>
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
}
