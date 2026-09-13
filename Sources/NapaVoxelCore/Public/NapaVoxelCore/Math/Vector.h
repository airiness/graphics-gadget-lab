#pragma once

#include <algorithm>
#include <cmath>
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

	[[nodiscard]] constexpr Double3 operator-(Double3 lhs, Double3 rhs) noexcept
	{
		return {
			lhs.m_X - rhs.m_X,
			lhs.m_Y - rhs.m_Y,
			lhs.m_Z - rhs.m_Z,
		};
	}

	[[nodiscard]] constexpr double Dot(Double3 lhs, Double3 rhs) noexcept
	{
		return lhs.m_X * rhs.m_X + lhs.m_Y * rhs.m_Y + lhs.m_Z * rhs.m_Z;
	}

	[[nodiscard]] constexpr Double3 Cross(Double3 lhs, Double3 rhs) noexcept
	{
		return {
			lhs.m_Y * rhs.m_Z - lhs.m_Z * rhs.m_Y,
			lhs.m_Z * rhs.m_X - lhs.m_X * rhs.m_Z,
			lhs.m_X * rhs.m_Y - lhs.m_Y * rhs.m_X,
		};
	}

	[[nodiscard]] inline bool IsFinite(Double3 value) noexcept
	{
		return std::isfinite(value.m_X) && std::isfinite(value.m_Y) &&
			std::isfinite(value.m_Z);
	}

	[[nodiscard]] inline bool TryNormalize(Double3 value, Double3& normalized) noexcept
	{
		if (!IsFinite(value))
		{
			return false;
		}
		const double scale = std::max({
			std::abs(value.m_X), std::abs(value.m_Y), std::abs(value.m_Z),
			});
		if (scale <= 0.0)
		{
			return false;
		}

		const Double3 scaled{
			value.m_X / scale,
			value.m_Y / scale,
			value.m_Z / scale,
		};
		const double inverseLength = 1.0 / std::sqrt(Dot(scaled, scaled));
		const Double3 prepared{
			scaled.m_X * inverseLength,
			scaled.m_Y * inverseLength,
			scaled.m_Z * inverseLength,
		};
		if (!std::isfinite(inverseLength) || !IsFinite(prepared))
		{
			return false;
		}

		normalized = prepared;
		return true;
	}

	static_assert(std::is_standard_layout_v<Double3>);
	static_assert(std::is_trivially_copyable_v<Double3>);
}
