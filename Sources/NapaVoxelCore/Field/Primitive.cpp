#include "NapaVoxelCore/Field/Primitive.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace napa::voxel
{
	namespace
	{
		[[nodiscard]] bool IsFinite(Double3 value) noexcept
		{
			return
				std::isfinite(value.m_X) &&
				std::isfinite(value.m_Y) &&
				std::isfinite(value.m_Z);
		}

		[[nodiscard]] bool HasPositiveComponents(
			Double3 value) noexcept
		{
			return
				value.m_X > 0.0 &&
				value.m_Y > 0.0 &&
				value.m_Z > 0.0;
		}

		[[nodiscard]] double EvaluateSphere(
			const SpherePrimitive& sphere,
			Double3 position) noexcept
		{
			const double x = position.m_X - sphere.m_Center.m_X;
			const double y = position.m_Y - sphere.m_Center.m_Y;
			const double z = position.m_Z - sphere.m_Center.m_Z;
			return std::sqrt(x * x + y * y + z * z) -
				sphere.m_Radius;
		}

		[[nodiscard]] double EvaluateBox(
			Double3 center,
			Double3 halfExtents,
			Double3 position) noexcept
		{
			const double x =
				std::abs(position.m_X - center.m_X) -
				halfExtents.m_X;
			const double y =
				std::abs(position.m_Y - center.m_Y) -
				halfExtents.m_Y;
			const double z =
				std::abs(position.m_Z - center.m_Z) -
				halfExtents.m_Z;

			const double outsideX = std::max(x, 0.0);
			const double outsideY = std::max(y, 0.0);
			const double outsideZ = std::max(z, 0.0);
			const double outside = std::sqrt(
				outsideX * outsideX +
				outsideY * outsideY +
				outsideZ * outsideZ);
			const double inside = std::min(
				std::max(x, std::max(y, z)),
				0.0);
			return outside + inside;
		}
	}

	ValidationResult ValidatePrimitive(
		const PrimitiveDesc& primitive) noexcept
	{
		if (!IsKnownVoxelMaterial(primitive.m_Material))
		{
			return { ValidationError::InvalidVoxelMaterial };
		}
		if (primitive.m_Material == VoxelMaterial::Empty)
		{
			return { ValidationError::EmptyPrimitiveMaterial };
		}

		switch (primitive.m_Shape)
		{
		case PrimitiveShape::Sphere:
			if (!IsFinite(primitive.m_Parameters.m_Sphere.m_Center))
			{
				return {
					ValidationError::NonFinitePrimitivePosition,
				};
			}
			if (!std::isfinite(
				primitive.m_Parameters.m_Sphere.m_Radius))
			{
				return { ValidationError::NonFinitePrimitiveSize };
			}
			if (primitive.m_Parameters.m_Sphere.m_Radius <= 0.0)
			{
				return {
					ValidationError::NonPositiveSphereRadius,
				};
			}
			return {};

		case PrimitiveShape::AxisAlignedBox:
			if (!IsFinite(
				primitive.m_Parameters.m_AxisAlignedBox.m_Center))
			{
				return {
					ValidationError::NonFinitePrimitivePosition,
				};
			}
			if (!IsFinite(
				primitive.m_Parameters.m_AxisAlignedBox.m_HalfExtents))
			{
				return { ValidationError::NonFinitePrimitiveSize };
			}
			if (!HasPositiveComponents(
				primitive.m_Parameters.m_AxisAlignedBox.m_HalfExtents))
			{
				return {
					ValidationError::NonPositivePrimitiveExtent,
				};
			}
			return {};

		case PrimitiveShape::GroundSlab:
			if (!IsFinite(
				primitive.m_Parameters.m_GroundSlab.m_Center))
			{
				return {
					ValidationError::NonFinitePrimitivePosition,
				};
			}
			if (!IsFinite(
				primitive.m_Parameters.m_GroundSlab.m_HalfExtents))
			{
				return { ValidationError::NonFinitePrimitiveSize };
			}
			if (!HasPositiveComponents(
				primitive.m_Parameters.m_GroundSlab.m_HalfExtents))
			{
				return {
					ValidationError::NonPositivePrimitiveExtent,
				};
			}
			return {};
		}

		return { ValidationError::InvalidPrimitiveShape };
	}

	ValidationResult ValidatePrimitiveSet(
		std::span<const PrimitiveDesc> primitives) noexcept
	{
		for (std::size_t index = 0;
			index < primitives.size();
			++index)
		{
			const ValidationResult primitiveResult =
				ValidatePrimitive(primitives[index]);
			if (primitiveResult.Failed())
			{
				return primitiveResult;
			}

			for (std::size_t previous = 0;
				previous < index;
				++previous)
			{
				if (primitives[previous].m_StableId ==
					primitives[index].m_StableId)
				{
					return {
						ValidationError::DuplicatePrimitiveStableId,
					};
				}
			}
		}

		return {};
	}

	ValidationResult EvaluatePrimitiveSignedDistance(
		const PrimitiveDesc& primitive,
		Double3 position,
		double& signedDistance) noexcept
	{
		const ValidationResult primitiveResult =
			ValidatePrimitive(primitive);
		if (primitiveResult.Failed())
		{
			return primitiveResult;
		}
		if (!IsFinite(position))
		{
			return {
				ValidationError::NonFinitePrimitivePosition,
			};
		}

		double evaluated = 0.0;
		switch (primitive.m_Shape)
		{
		case PrimitiveShape::Sphere:
			evaluated = EvaluateSphere(
				primitive.m_Parameters.m_Sphere,
				position);
			break;

		case PrimitiveShape::AxisAlignedBox:
			evaluated = EvaluateBox(
				primitive.m_Parameters.m_AxisAlignedBox.m_Center,
				primitive.m_Parameters.m_AxisAlignedBox.m_HalfExtents,
				position);
			break;

		case PrimitiveShape::GroundSlab:
			evaluated = EvaluateBox(
				primitive.m_Parameters.m_GroundSlab.m_Center,
				primitive.m_Parameters.m_GroundSlab.m_HalfExtents,
				position);
			break;

		default:
			return { ValidationError::InvalidPrimitiveShape };
		}

		if (!std::isfinite(evaluated))
		{
			return { ValidationError::NonFiniteSignedDistance };
		}

		signedDistance = evaluated;
		return {};
	}
}
