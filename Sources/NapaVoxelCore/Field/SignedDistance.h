#pragma once

#include "NapaVoxelCore/Math/Vector.h"

#include <cmath>

namespace napa::voxel::detail
{
	[[nodiscard]] inline double EvaluateSphereSignedDistance(
		Double3 center, double radius, Double3 position) noexcept
	{
		const Double3 offset = position - center;
		return std::sqrt(Dot(offset, offset)) - radius;
	}
}
