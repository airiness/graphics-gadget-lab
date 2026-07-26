#pragma once

#include <cstdint>

namespace napa::voxel
{
	struct CellCoord
	{
		std::int32_t m_X = 0;
		std::int32_t m_Y = 0;
		std::int32_t m_Z = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const CellCoord&,
			const CellCoord&) noexcept = default;
	};

	struct CellAabb
	{
		CellCoord m_Min{};
		CellCoord m_MaxExclusive{};

		[[nodiscard]] constexpr bool IsEmpty() const noexcept
		{
			return
				m_Min.m_X >= m_MaxExclusive.m_X ||
				m_Min.m_Y >= m_MaxExclusive.m_Y ||
				m_Min.m_Z >= m_MaxExclusive.m_Z;
		}
	};
}
