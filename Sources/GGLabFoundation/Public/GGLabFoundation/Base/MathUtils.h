#pragma once

#include "GGLabFoundation/Base/TypeUtils.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace gglab::utils
{
	template <UnsignedInteger T> constexpr T AlignUp(T value, T multiple) noexcept
	{
		if (multiple == 0)
		{
			return value;
		}
		const T result = value % multiple;
		return result ? (value + multiple - result) : value;
	}

	template <typename T> [[nodiscard]] T* AlignUp(T* value, std::size_t alignment) noexcept
		requires std::is_object_v<T>
	{
		const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(value);
		return reinterpret_cast<T*>(AlignUp(address, static_cast<std::uintptr_t>(alignment)));
	}

	template <UnsignedInteger T> constexpr T AlignDown(T value, T multiple) noexcept
	{
		if (multiple == 0)
		{
			return value;
		}
		return value - (value % multiple);
	}

	template <UnsignedInteger T> constexpr bool IsPow2(T value) noexcept
	{
		return (value != 0) && ((value & (value - 1)) == 0);
	}

	template <UnsignedInteger T> constexpr T AlignUpPow2(T value, T alignment) noexcept
	{
		GGLAB_ASSERT_MSG(IsPow2(alignment), "Alignment must be a power of two.");
		return (alignment == 0) ? value : ((value + alignment - 1) & ~(alignment - 1));
	}
}
