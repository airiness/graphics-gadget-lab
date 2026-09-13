#include "NapaVoxelCore/Edit/VoxelDamage.h"

#include "NapaVoxelCore/Validation/CheckedArithmetic.h"
#include "NapaVoxelCore/World/VoxelSample.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace napa::voxel
{
	ValidationResult BuildVoxelDamageMarkerSnapshot(
		const VoxelWorld& world, std::uint64_t sourceWorldVoxelRevision,
		VoxelDamageMarkerSnapshot& snapshot, std::size_t markerLimit)
	{
		if (sourceWorldVoxelRevision == 0 ||
			sourceWorldVoxelRevision != world.GetWorldVoxelRevision())
		{
			return { ValidationError::MismatchedDamageMarkerSourceRevision };
		}

		VoxelDamageMarkerSnapshot prepared{
			.m_SourceWorldVoxelRevision = sourceWorldVoxelRevision,
		};
		prepared.m_Markers.reserve(std::min(markerLimit, DefaultDamageMarkerLimit));
		const double voxelSize = static_cast<double>(world.GetConfig().m_VoxelSize);
		const SampleAabb bounds = world.GetLogicalSampleBounds();
		for (std::int32_t z = bounds.m_Min.m_Z; z < bounds.m_MaxExclusive.m_Z; ++z)
		{
			for (std::int32_t y = bounds.m_Min.m_Y; y < bounds.m_MaxExclusive.m_Y; ++y)
			{
				for (std::int32_t x = bounds.m_Min.m_X; x < bounds.m_MaxExclusive.m_X; ++x)
				{
					VoxelSample sample{};
					const SampleCoord coordinate{ x, y, z };
					const ValidationResult readResult =
						world.ReadCurrentSample(coordinate, sample);
					if (readResult.Failed())
					{
						return readResult;
					}
					if (sample.m_Material != VoxelMaterial::Stone || sample.m_Damage == 0)
					{
						continue;
					}

					const auto nextCount = CheckedAdd(
						prepared.m_TotalDamagedSampleCount, std::uint64_t{ 1 });
					if (!nextCount.has_value())
					{
						return { ValidationError::ArithmeticOverflow };
					}
					prepared.m_TotalDamagedSampleCount = *nextCount;
					if (prepared.m_Markers.size() >= markerLimit)
					{
						continue;
					}

					const Double3 worldPosition{
						static_cast<double>(x) * voxelSize,
						static_cast<double>(y) * voxelSize,
						static_cast<double>(z) * voxelSize,
					};
					if (!std::isfinite(worldPosition.m_X) ||
						!std::isfinite(worldPosition.m_Y) ||
						!std::isfinite(worldPosition.m_Z))
					{
						return { ValidationError::ArithmeticOverflow };
					}
					prepared.m_Markers.push_back({
						.m_Sample = coordinate,
						.m_WorldPosition = worldPosition,
						.m_Damage = sample.m_Damage,
						});
				}
			}
		}

		prepared.m_Truncated = prepared.m_Markers.size() <
			prepared.m_TotalDamagedSampleCount;
		snapshot = std::move(prepared);
		return {};
	}
}
