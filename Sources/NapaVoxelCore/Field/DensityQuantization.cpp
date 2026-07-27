#include "NapaVoxelCore/Field/DensityQuantization.h"

#include "NapaVoxelCore/World/VoxelSample.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace napa::voxel
{
	namespace
	{
		[[nodiscard]] std::int64_t RoundHalfAwayFromZeroUnchecked(
			double value) noexcept
		{
			const double roundedValue = value >= 0.0
				? std::floor(value + 0.5)
				: std::ceil(value - 0.5);
			return static_cast<std::int64_t>(roundedValue);
		}
	}

	ValidationResult RoundHalfAwayFromZero(
		double value,
		std::int64_t& rounded) noexcept
	{
		if (!std::isfinite(value))
		{
			return { ValidationError::NonFiniteQuantizationInput };
		}

		constexpr double MinimumInt64 = -9223372036854775808.0;
		constexpr double MaximumInt64Exclusive = 9223372036854775808.0;
		const double roundedValue = value >= 0.0
			? std::floor(value + 0.5)
			: std::ceil(value - 0.5);
		if (roundedValue < MinimumInt64 ||
			roundedValue >= MaximumInt64Exclusive)
		{
			return { ValidationError::ArithmeticOverflow };
		}

		rounded = RoundHalfAwayFromZeroUnchecked(value);
		return {};
	}

	ValidationResult PrepareDensityQuantizationContext(
		float voxelSize,
		float surfaceBandVoxels,
		DensityQuantizationContext& context) noexcept
	{
		if (!std::isfinite(voxelSize))
		{
			return { ValidationError::NonFiniteVoxelSize };
		}
		if (voxelSize <= 0.0f)
		{
			return { ValidationError::NonPositiveVoxelSize };
		}
		if (!std::isfinite(surfaceBandVoxels))
		{
			return {
				ValidationError::NonFiniteSurfaceBandVoxels,
			};
		}
		if (surfaceBandVoxels <= 0.0f)
		{
			return {
				ValidationError::NonPositiveSurfaceBandVoxels,
			};
		}

		const double bandWorld =
			static_cast<double>(voxelSize) *
			static_cast<double>(surfaceBandVoxels);
		DensityQuantizationContext prepared;
		prepared.m_BandWorld = bandWorld;
		prepared.m_IsPrepared = true;
		context = prepared;
		return {};
	}

	ValidationResult QuantizeSignedDistance(
		double signedDistance,
		const DensityQuantizationContext& context,
		std::uint8_t& density) noexcept
	{
		if (!context.m_IsPrepared)
		{
			return {
				ValidationError::UnpreparedDensityQuantizationContext,
			};
		}
		if (!std::isfinite(signedDistance))
		{
			return { ValidationError::NonFiniteQuantizationInput };
		}

		const double normalized =
			-signedDistance / context.m_BandWorld;
		const double densityValue =
			static_cast<double>(IsoValue) + normalized * 127.0;
		const double clamped = std::clamp(
			densityValue,
			0.0,
			255.0);
		density = static_cast<std::uint8_t>(
			RoundHalfAwayFromZeroUnchecked(clamped));
		return {};
	}

	ValidationResult QuantizeSignedDistance(
		double signedDistance,
		float voxelSize,
		float surfaceBandVoxels,
		std::uint8_t& density) noexcept
	{
		DensityQuantizationContext context;
		const ValidationResult contextResult =
			PrepareDensityQuantizationContext(
				voxelSize,
				surfaceBandVoxels,
				context);
		if (contextResult.Failed())
		{
			return contextResult;
		}

		return QuantizeSignedDistance(
			signedDistance,
			context,
			density);
	}
}
