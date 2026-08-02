#include "NapaVoxelCore/Field/Primitive.h"

#include "NapaVoxelCore/Field/DensityQuantization.h"
#include "NapaVoxelCore/Field/SignedDistance.h"
#include "NapaVoxelCore/Hash/VoxelWorldHash.h"
#include "NapaVoxelCore/World/VoxelWorld.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace napa::voxel
{
	namespace
	{
		[[nodiscard]] bool IsFinite(Double3 value) noexcept
		{
			return std::isfinite(value.m_X) && std::isfinite(value.m_Y) && std::isfinite(value.m_Z);
		}

		[[nodiscard]] bool HasPositiveComponents(Double3 value) noexcept
		{
			return value.m_X > 0.0 && value.m_Y > 0.0 && value.m_Z > 0.0;
		}

		[[nodiscard]] double EvaluateSphere(
			const SpherePrimitive& sphere, Double3 position) noexcept
		{
			return detail::EvaluateSphereSignedDistance(
				sphere.m_Center, sphere.m_Radius, position);
		}

		[[nodiscard]] double EvaluateBox(
			Double3 center, Double3 halfExtents, Double3 position) noexcept
		{
			const double x = std::abs(position.m_X - center.m_X) - halfExtents.m_X;
			const double y = std::abs(position.m_Y - center.m_Y) - halfExtents.m_Y;
			const double z = std::abs(position.m_Z - center.m_Z) - halfExtents.m_Z;

			const double outsideX = std::max(x, 0.0);
			const double outsideY = std::max(y, 0.0);
			const double outsideZ = std::max(z, 0.0);
			const double outside =
				std::sqrt(outsideX * outsideX + outsideY * outsideY + outsideZ * outsideZ);
			const double inside = std::min(std::max(x, std::max(y, z)), 0.0);
			return outside + inside;
		}

		[[nodiscard]] double EvaluateValidatedPrimitive(
			const PrimitiveDesc& primitive, Double3 position) noexcept
		{
			switch (primitive.m_Shape)
			{
			case PrimitiveShape::Sphere:
				return EvaluateSphere(primitive.m_Parameters.m_Sphere, position);

			case PrimitiveShape::AxisAlignedBox:
				return EvaluateBox(primitive.m_Parameters.m_AxisAlignedBox.m_Center,
					primitive.m_Parameters.m_AxisAlignedBox.m_HalfExtents, position);

			case PrimitiveShape::GroundSlab:
				return EvaluateBox(primitive.m_Parameters.m_GroundSlab.m_Center,
					primitive.m_Parameters.m_GroundSlab.m_HalfExtents, position);
			}

			return std::numeric_limits<double>::quiet_NaN();
		}

		[[nodiscard]] bool PrecedesOnEqualDistance(
			const PrimitiveDesc& candidate, const PrimitiveDesc& current) noexcept
		{
			if (candidate.m_Priority.m_Value != current.m_Priority.m_Value)
			{
				return candidate.m_Priority.m_Value > current.m_Priority.m_Value;
			}

			return candidate.m_StableId.m_Value < current.m_StableId.m_Value;
		}

		[[nodiscard]] ValidationResult EvaluateValidatedPrimitiveSet(
			const DensityQuantizationContext& quantizationContext,
			std::span<const PrimitiveDesc> primitives, Double3 position,
			VoxelSample& prepared) noexcept
		{
			const PrimitiveDesc* selected = nullptr;
			double minimumSignedDistance = 0.0;
			for (const PrimitiveDesc& primitive : primitives)
			{
				const double signedDistance = EvaluateValidatedPrimitive(primitive, position);
				if (!std::isfinite(signedDistance))
				{
					return {
						ValidationError::NonFiniteSignedDistance,
					};
				}

				if (selected == nullptr || signedDistance < minimumSignedDistance ||
					(signedDistance == minimumSignedDistance &&
						PrecedesOnEqualDistance(primitive, *selected)))
				{
					selected = &primitive;
					minimumSignedDistance = signedDistance;
				}
			}

			if (selected == nullptr)
			{
				prepared = DefaultVoxelSample;
				return {};
			}

			std::uint8_t density = 0;
			const ValidationResult quantizationResult =
				QuantizeSignedDistance(minimumSignedDistance, quantizationContext, density);
			if (quantizationResult.Failed())
			{
				return quantizationResult;
			}

			return PrepareVoxelSampleForStorage(
				{
					.m_Density = density,
					.m_Material = selected->m_Material,
					.m_Damage = 0,
				},
				prepared);
		}

		[[nodiscard]] bool IsOuterCellLayerSample(
			SampleCoord coordinate, const CellAabb& cellBounds) noexcept
		{
			const std::int64_t x = coordinate.m_X;
			const std::int64_t y = coordinate.m_Y;
			const std::int64_t z = coordinate.m_Z;
			return x <= static_cast<std::int64_t>(cellBounds.m_Min.m_X) + 1 ||
				y <= static_cast<std::int64_t>(cellBounds.m_Min.m_Y) + 1 ||
				z <= static_cast<std::int64_t>(cellBounds.m_Min.m_Z) + 1 ||
				x >= static_cast<std::int64_t>(cellBounds.m_MaxExclusive.m_X) - 1 ||
				y >= static_cast<std::int64_t>(cellBounds.m_MaxExclusive.m_Y) - 1 ||
				z >= static_cast<std::int64_t>(cellBounds.m_MaxExclusive.m_Z) - 1;
		}
	}

	ValidationResult ValidatePrimitive(const PrimitiveDesc& primitive) noexcept
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
			if (!std::isfinite(primitive.m_Parameters.m_Sphere.m_Radius))
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
			if (!IsFinite(primitive.m_Parameters.m_AxisAlignedBox.m_Center))
			{
				return {
					ValidationError::NonFinitePrimitivePosition,
				};
			}
			if (!IsFinite(primitive.m_Parameters.m_AxisAlignedBox.m_HalfExtents))
			{
				return { ValidationError::NonFinitePrimitiveSize };
			}
			if (!HasPositiveComponents(primitive.m_Parameters.m_AxisAlignedBox.m_HalfExtents))
			{
				return {
					ValidationError::NonPositivePrimitiveExtent,
				};
			}
			return {};

		case PrimitiveShape::GroundSlab:
			if (!IsFinite(primitive.m_Parameters.m_GroundSlab.m_Center))
			{
				return {
					ValidationError::NonFinitePrimitivePosition,
				};
			}
			if (!IsFinite(primitive.m_Parameters.m_GroundSlab.m_HalfExtents))
			{
				return { ValidationError::NonFinitePrimitiveSize };
			}
			if (!HasPositiveComponents(primitive.m_Parameters.m_GroundSlab.m_HalfExtents))
			{
				return {
					ValidationError::NonPositivePrimitiveExtent,
				};
			}
			return {};
		}

		return { ValidationError::InvalidPrimitiveShape };
	}

	ValidationResult ValidatePrimitiveSet(std::span<const PrimitiveDesc> primitives) noexcept
	{
		for (std::size_t index = 0; index < primitives.size(); ++index)
		{
			const ValidationResult primitiveResult = ValidatePrimitive(primitives[index]);
			if (primitiveResult.Failed())
			{
				return primitiveResult;
			}

			for (std::size_t previous = 0; previous < index; ++previous)
			{
				if (primitives[previous].m_StableId == primitives[index].m_StableId)
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
		const PrimitiveDesc& primitive, Double3 position, double& signedDistance) noexcept
	{
		const ValidationResult primitiveResult = ValidatePrimitive(primitive);
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

		const double evaluated = EvaluateValidatedPrimitive(primitive, position);

		if (!std::isfinite(evaluated))
		{
			return { ValidationError::NonFiniteSignedDistance };
		}

		signedDistance = evaluated;
		return {};
	}

	ValidationResult ValidateEmptyCellSafetyMargin(const VoxelWorld& world) noexcept
	{
		const SampleAabb sampleBounds = world.GetLogicalSampleBounds();
		const CellAabb& cellBounds = world.GetConfig().m_LogicalCellBounds;
		for (std::int64_t z = sampleBounds.m_Min.m_Z; z < sampleBounds.m_MaxExclusive.m_Z; ++z)
		{
			for (std::int64_t y = sampleBounds.m_Min.m_Y; y < sampleBounds.m_MaxExclusive.m_Y; ++y)
			{
				for (std::int64_t x = sampleBounds.m_Min.m_X; x < sampleBounds.m_MaxExclusive.m_X;
					++x)
				{
					const SampleCoord coordinate{
						static_cast<std::int32_t>(x),
						static_cast<std::int32_t>(y),
						static_cast<std::int32_t>(z),
					};
					if (!IsOuterCellLayerSample(coordinate, cellBounds))
					{
						continue;
					}

					VoxelSample sample{};
					const ValidationResult readResult = world.ReadCurrentSample(coordinate, sample);
					if (readResult.Failed())
					{
						return readResult;
					}
					if (sample.m_Density >= IsoValue)
					{
						return {
							ValidationError::EmptySafetyMarginViolation,
						};
					}
				}
			}
		}

		return {};
	}

	ValidationResult GeneratePrimitiveVoxelWorld(const VoxelWorldConfig& config,
		std::span<const PrimitiveDesc> primitives, std::unique_ptr<VoxelWorld>& world,
		PrimitiveWorldGenerationResult& result)
	{
		const ValidationResult primitiveResult = ValidatePrimitiveSet(primitives);
		if (primitiveResult.Failed())
		{
			return primitiveResult;
		}

		std::vector<PrimitiveDesc> orderedPrimitives{
			primitives.begin(),
			primitives.end(),
		};
		std::sort(orderedPrimitives.begin(), orderedPrimitives.end(),
			[](const PrimitiveDesc& lhs, const PrimitiveDesc& rhs)
			{ return lhs.m_StableId.m_Value < rhs.m_StableId.m_Value; });

		std::unique_ptr<VoxelWorld> generatedWorld;
		const ValidationResult createResult = VoxelWorld::Create(config, generatedWorld);
		if (createResult.Failed())
		{
			return createResult;
		}

		const SampleAabb bounds = generatedWorld->GetLogicalSampleBounds();
		DensityQuantizationContext quantizationContext;
		const ValidationResult quantizationContextResult = PrepareDensityQuantizationContext(
			config.m_VoxelSize, config.m_SurfaceBandVoxels, quantizationContext);
		if (quantizationContextResult.Failed())
		{
			return quantizationContextResult;
		}
		const std::span<const PrimitiveDesc> orderedSpan{
			orderedPrimitives,
		};
		for (std::int64_t z = bounds.m_Min.m_Z; z < bounds.m_MaxExclusive.m_Z; ++z)
		{
			for (std::int64_t y = bounds.m_Min.m_Y; y < bounds.m_MaxExclusive.m_Y; ++y)
			{
				for (std::int64_t x = bounds.m_Min.m_X; x < bounds.m_MaxExclusive.m_X; ++x)
				{
					const SampleCoord coordinate{
						static_cast<std::int32_t>(x),
						static_cast<std::int32_t>(y),
						static_cast<std::int32_t>(z),
					};
					const Double3 position{
						static_cast<double>(x) * static_cast<double>(config.m_VoxelSize),
						static_cast<double>(y) * static_cast<double>(config.m_VoxelSize),
						static_cast<double>(z) * static_cast<double>(config.m_VoxelSize),
					};
					VoxelSample prepared{};
					const ValidationResult sampleResult = EvaluateValidatedPrimitiveSet(
						quantizationContext, orderedSpan, position, prepared);
					if (sampleResult.Failed())
					{
						return sampleResult;
					}

					const ValidationResult initializeResult =
						generatedWorld->InitializePreparedSample(coordinate, prepared);
					if (initializeResult.Failed())
					{
						return initializeResult;
					}
				}
			}
		}

		const ValidationResult safetyResult = ValidateEmptyCellSafetyMargin(*generatedWorld);
		if (safetyResult.Failed())
		{
			return safetyResult;
		}

		generatedWorld->CommitGeneratedOriginalState();
		PrimitiveWorldGenerationResult generatedResult{};
		const ValidationResult hashResult =
			ComputeLogicalVoxelWorldHash(*generatedWorld, generatedResult.m_InitialVoxelHash);
		if (hashResult.Failed())
		{
			return hashResult;
		}

		result = generatedResult;
		world = std::move(generatedWorld);
		return {};
	}
}
