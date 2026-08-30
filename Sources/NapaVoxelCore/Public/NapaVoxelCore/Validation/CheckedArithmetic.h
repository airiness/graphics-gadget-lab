#pragma once

#include <concepts>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace napa::voxel
{
	template <std::integral Integer>
		requires(!std::same_as<std::remove_cv_t<Integer>, bool>)
	[[nodiscard]] constexpr std::optional<Integer> CheckedAdd(Integer lhs, Integer rhs) noexcept
	{
		using Limits = std::numeric_limits<Integer>;

		if constexpr (std::is_unsigned_v<Integer>)
		{
			if (rhs > Limits::max() - lhs)
			{
				return std::nullopt;
			}
		}
		else
		{
			if ((rhs > 0 && lhs > Limits::max() - rhs) || (rhs < 0 && lhs < Limits::min() - rhs))
			{
				return std::nullopt;
			}
		}

		return lhs + rhs;
	}

	template <std::integral Integer>
		requires(!std::same_as<std::remove_cv_t<Integer>, bool>)
	[[nodiscard]] constexpr std::optional<Integer> CheckedMul(Integer lhs, Integer rhs) noexcept
	{
		using Limits = std::numeric_limits<Integer>;

		if (lhs == 0 || rhs == 0)
		{
			return Integer{ 0 };
		}

		if constexpr (std::is_unsigned_v<Integer>)
		{
			if (lhs > Limits::max() / rhs)
			{
				return std::nullopt;
			}
		}
		else if (lhs > 0)
		{
			if ((rhs > 0 && lhs > Limits::max() / rhs) || (rhs < 0 && rhs < Limits::min() / lhs))
			{
				return std::nullopt;
			}
		}
		else
		{
			if ((rhs > 0 && lhs < Limits::min() / rhs) || (rhs < 0 && lhs < Limits::max() / rhs))
			{
				return std::nullopt;
			}
		}

		return lhs * rhs;
	}

	template <std::integral Target, std::integral Source>
		requires(!std::same_as<std::remove_cv_t<Target>, bool> &&
	!std::same_as<std::remove_cv_t<Source>, bool>)
		[[nodiscard]] constexpr std::optional<Target> CheckedNarrow(Source value) noexcept
	{
		if (!std::in_range<Target>(value))
		{
			return std::nullopt;
		}

		return static_cast<Target>(value);
	}
}
