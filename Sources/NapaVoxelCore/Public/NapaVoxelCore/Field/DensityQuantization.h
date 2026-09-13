#pragma once

#include "NapaVoxelCore/Validation/ValidationResult.h"

#include <cstdint>

namespace napa::voxel
{
	class DensityQuantizationContext final
	{
	public:
		DensityQuantizationContext() = default;

	private:
		friend ValidationResult PrepareDensityQuantizationContext(
			float voxelSize, float surfaceBandVoxels, DensityQuantizationContext& context) noexcept;
		friend ValidationResult QuantizeSignedDistance(double signedDistance,
			const DensityQuantizationContext& context, std::uint8_t& density) noexcept;

		double m_BandWorld = 0.0;
		bool m_IsPrepared = false;
	};

	[[nodiscard]] ValidationResult RoundHalfAwayFromZero(
		double value, std::int64_t& rounded) noexcept;
	[[nodiscard]] ValidationResult PrepareDensityQuantizationContext(
		float voxelSize, float surfaceBandVoxels, DensityQuantizationContext& context) noexcept;
	[[nodiscard]] ValidationResult QuantizeSignedDistance(double signedDistance,
		const DensityQuantizationContext& context, std::uint8_t& density) noexcept;
	[[nodiscard]] ValidationResult QuantizeSignedDistance(double signedDistance, float voxelSize,
		float surfaceBandVoxels, std::uint8_t& density) noexcept;
}
