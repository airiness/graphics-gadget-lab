#pragma once

#include <type_traits>

namespace napa::voxel
{
	struct Double3
	{
		double m_X = 0.0;
		double m_Y = 0.0;
		double m_Z = 0.0;

		[[nodiscard]] friend constexpr bool operator==(
			const Double3&, const Double3&) noexcept = default;
	};

	static_assert(std::is_standard_layout_v<Double3>);
	static_assert(std::is_trivially_copyable_v<Double3>);
}
