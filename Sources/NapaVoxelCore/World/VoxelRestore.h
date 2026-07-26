#pragma once

#include "NapaVoxelCore/World/Coordinates.h"

#include <vector>

namespace napa::voxel
{
	struct RestoreResult
	{
		std::vector<SampleCoord> m_ChangedSampleCoordinates;

		[[nodiscard]] bool Changed() const noexcept
		{
			return !m_ChangedSampleCoordinates.empty();
		}
	};
}
