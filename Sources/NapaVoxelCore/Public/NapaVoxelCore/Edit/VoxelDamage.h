#pragma once

#include "NapaVoxelCore/Math/Vector.h"
#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace napa::voxel
{
	class VoxelWorld;

	inline constexpr std::size_t DefaultDamageMarkerLimit = 2048;

	struct VoxelDamageMarker
	{
		SampleCoord m_Sample{};
		Double3 m_WorldPosition{};
		std::uint8_t m_Damage = 0;

		[[nodiscard]] friend bool operator==(
			const VoxelDamageMarker&, const VoxelDamageMarker&) noexcept = default;
	};

	struct VoxelDamageMarkerSnapshot
	{
		std::uint64_t m_SourceWorldVoxelRevision = 0;
		std::uint64_t m_TotalDamagedSampleCount = 0;
		bool m_Truncated = false;
		std::vector<VoxelDamageMarker> m_Markers;

		[[nodiscard]] friend bool operator==(
			const VoxelDamageMarkerSnapshot&,
			const VoxelDamageMarkerSnapshot&) noexcept = default;
	};

	[[nodiscard]] ValidationResult BuildVoxelDamageMarkerSnapshot(
		const VoxelWorld& world, std::uint64_t sourceWorldVoxelRevision,
		VoxelDamageMarkerSnapshot& snapshot,
		std::size_t markerLimit = DefaultDamageMarkerLimit);

	static_assert(std::is_standard_layout_v<VoxelDamageMarker>);
	static_assert(std::is_trivially_copyable_v<VoxelDamageMarker>);
}
