#pragma once
#include <type_traits>

namespace gglab::utils
{
	template<typename T>
	concept UnsignedInt = std::is_unsigned_v<T> && std::is_integral_v<T>;
}