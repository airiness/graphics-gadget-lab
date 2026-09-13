#include "NapaVoxelCore/Edit/SphereEdit.h"

#include "Field/SignedDistance.h"
#include "NapaVoxelCore/World/VoxelSample.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace napa::voxel
{
	namespace
	{
		[[nodiscard]] ValidationResult NarrowIntegralDouble(
			double value, std::int32_t& narrowed) noexcept
		{
			constexpr double Minimum =
				static_cast<double>(std::numeric_limits<std::int32_t>::min());
			constexpr double Maximum =
				static_cast<double>(std::numeric_limits<std::int32_t>::max());
			if (!std::isfinite(value) || value < Minimum || value > Maximum)
			{
				return { ValidationError::ArithmeticOverflow };
			}

			narrowed = static_cast<std::int32_t>(value);
			return {};
		}

		[[nodiscard]] ValidationResult ComputeAxisBounds(double center, double scanRadius,
			double voxelSize, std::int32_t& minimum, std::int32_t& maximumExclusive) noexcept
		{
			const double minimumWorld = center - scanRadius;
			const double maximumWorld = center + scanRadius;
			if (!std::isfinite(minimumWorld) || !std::isfinite(maximumWorld))
			{
				return { ValidationError::ArithmeticOverflow };
			}

			const double minimumSample = std::ceil(minimumWorld / voxelSize);
			const double maximumSampleExclusive =
				std::floor(maximumWorld / voxelSize) + 1.0;
			std::int32_t preparedMinimum = 0;
			std::int32_t preparedMaximumExclusive = 0;
			const ValidationResult minimumResult =
				NarrowIntegralDouble(minimumSample, preparedMinimum);
			if (minimumResult.Failed())
			{
				return minimumResult;
			}
			const ValidationResult maximumResult =
				NarrowIntegralDouble(maximumSampleExclusive, preparedMaximumExclusive);
			if (maximumResult.Failed())
			{
				return maximumResult;
			}

			minimum = preparedMinimum;
			maximumExclusive = preparedMaximumExclusive;
			return {};
		}

		[[nodiscard]] SampleAabb IntersectBounds(
			const SampleAabb& lhs, const SampleAabb& rhs) noexcept
		{
			return {
				.m_Min = {
					std::max(lhs.m_Min.m_X, rhs.m_Min.m_X),
					std::max(lhs.m_Min.m_Y, rhs.m_Min.m_Y),
					std::max(lhs.m_Min.m_Z, rhs.m_Min.m_Z),
				},
				.m_MaxExclusive = {
					std::min(lhs.m_MaxExclusive.m_X, rhs.m_MaxExclusive.m_X),
					std::min(lhs.m_MaxExclusive.m_Y, rhs.m_MaxExclusive.m_Y),
					std::min(lhs.m_MaxExclusive.m_Z, rhs.m_MaxExclusive.m_Z),
				},
			};
		}
	}

	ValidationResult ValidateEdit(const SphereEditRequest& request) noexcept
	{
		if (!IsFinite(request.m_Brush.m_CenterWorld))
		{
			return { ValidationError::NonFiniteEditPosition };
		}
		if (!std::isfinite(request.m_Brush.m_Radius))
		{
			return { ValidationError::NonFiniteEditRadius };
		}
		if (request.m_Brush.m_Radius <= 0.0)
		{
			return { ValidationError::NonPositiveEditRadius };
		}
		if (!std::isfinite(request.m_Brush.m_Strength))
		{
			return { ValidationError::NonFiniteEditStrength };
		}
		if (request.m_MaterialRules.m_StoneBreakThreshold == 0)
		{
			return { ValidationError::InvalidVoxelEditMaterialRules };
		}
		return {};
	}

	ValidationResult PrepareSphereEditContext(const VoxelWorldConfig& config,
		const SphereEditRequest& request, SphereEditContext& context) noexcept
	{
		const ValidationResult configResult = ValidateConfig(config);
		if (configResult.Failed())
		{
			return configResult;
		}
		const ValidationResult editResult = ValidateEdit(request);
		if (editResult.Failed())
		{
			return editResult;
		}

		SphereEditContext prepared{};
		prepared.m_Config = config;
		prepared.m_Request = request;
		prepared.m_Request.m_Brush.m_Strength =
			std::clamp(request.m_Brush.m_Strength, 0.0, 1.0);
		prepared.m_HasDensityPath = prepared.m_Request.m_Brush.m_Strength > 0.0;
		prepared.m_HasDamagePath = request.m_MaterialRules.m_DamagePerHit > 0;
		const ValidationResult quantizationResult = PrepareDensityQuantizationContext(
			config.m_VoxelSize, config.m_SurfaceBandVoxels, prepared.m_DensityQuantization);
		if (quantizationResult.Failed())
		{
			return quantizationResult;
		}
		const ValidationResult logicalBoundsResult = LogicalCellBoundsToSampleBounds(
			config.m_LogicalCellBounds, prepared.m_LogicalSampleBounds);
		if (logicalBoundsResult.Failed())
		{
			return logicalBoundsResult;
		}

		if (!prepared.m_HasDensityPath && !prepared.m_HasDamagePath)
		{
			prepared.m_IsPrepared = true;
			context = prepared;
			return {};
		}

		const double voxelSize = static_cast<double>(config.m_VoxelSize);
		const double bandWorld = voxelSize * static_cast<double>(config.m_SurfaceBandVoxels);
		const double densityScanRadius = request.m_Brush.m_Radius +
			bandWorld * (126.5 / 127.0);
		const double damageScanRadius = request.m_Brush.m_Radius +
			bandWorld * (0.5 / 127.0);
		const double scanRadius = std::max(
			prepared.m_HasDensityPath ? densityScanRadius : 0.0,
			prepared.m_HasDamagePath ? damageScanRadius : 0.0);
		if (!std::isfinite(scanRadius))
		{
			return { ValidationError::ArithmeticOverflow };
		}

		SampleAabb rawBounds{};
		const ValidationResult xResult = ComputeAxisBounds(request.m_Brush.m_CenterWorld.m_X,
			scanRadius, voxelSize, rawBounds.m_Min.m_X, rawBounds.m_MaxExclusive.m_X);
		if (xResult.Failed())
		{
			return xResult;
		}
		const ValidationResult yResult = ComputeAxisBounds(request.m_Brush.m_CenterWorld.m_Y,
			scanRadius, voxelSize, rawBounds.m_Min.m_Y, rawBounds.m_MaxExclusive.m_Y);
		if (yResult.Failed())
		{
			return yResult;
		}
		const ValidationResult zResult = ComputeAxisBounds(request.m_Brush.m_CenterWorld.m_Z,
			scanRadius, voxelSize, rawBounds.m_Min.m_Z, rawBounds.m_MaxExclusive.m_Z);
		if (zResult.Failed())
		{
			return zResult;
		}

		prepared.m_ScanBounds = IntersectBounds(rawBounds, prepared.m_LogicalSampleBounds);
		prepared.m_IsPrepared = true;
		context = prepared;
		return {};
	}

	ValidationResult EvaluateSphereEditSample(const SphereEditContext& context,
		SampleCoord sample, SphereEditSampleEvaluation& evaluation) noexcept
	{
		if (!context.m_IsPrepared)
		{
			return { ValidationError::UnpreparedSphereEditContext };
		}
		if (!context.m_LogicalSampleBounds.Contains(sample))
		{
			return { ValidationError::SampleOutsideLogicalBounds };
		}

		const double voxelSize = static_cast<double>(context.m_Config.m_VoxelSize);
		const Double3 sampleWorldPosition{
			static_cast<double>(sample.m_X) * voxelSize,
			static_cast<double>(sample.m_Y) * voxelSize,
			static_cast<double>(sample.m_Z) * voxelSize,
		};
		const double signedDistance = detail::EvaluateSphereSignedDistance(
			context.m_Request.m_Brush.m_CenterWorld,
			context.m_Request.m_Brush.m_Radius, sampleWorldPosition);
		if (!std::isfinite(signedDistance))
		{
			return { ValidationError::NonFiniteSignedDistance };
		}

		SphereEditSampleEvaluation prepared{};
		const ValidationResult densityResult = QuantizeSignedDistance(
			signedDistance, context.m_DensityQuantization, prepared.m_BrushDensity);
		if (densityResult.Failed())
		{
			return densityResult;
		}
		prepared.m_DensityPathEligible =
			context.m_HasDensityPath && prepared.m_BrushDensity >= 2;
		prepared.m_DamagePathEligible =
			context.m_HasDamagePath && prepared.m_BrushDensity >= IsoValue;
		evaluation = prepared;
		return {};
	}
}
