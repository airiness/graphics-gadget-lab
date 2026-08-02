#pragma once

#include "NapaVoxelCore/Math/Vector.h"

#include <cmath>

namespace napa::voxel::detail
{
	[[nodiscard]] inline double EvaluateSphereSignedDistance(
		Double3 center, double radius, Double3 position) noexcept
	{
		const double x = position.m_X - center.m_X;
		const double y = position.m_Y - center.m_Y;
		const double z = position.m_Z - center.m_Z;
		return std::sqrt(x * x + y * y + z * z) - radius;
	}
}
