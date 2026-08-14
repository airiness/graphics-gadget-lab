#pragma once
#include <type_traits>

namespace gglab
{
#define GGLAB_ENUM_FLAGS(EnumType)                                                  \
constexpr inline EnumType operator|(EnumType a, EnumType b) noexcept {				\
	using U = std::underlying_type_t<EnumType>;										\
	return static_cast<EnumType>(static_cast<U>(a) | static_cast<U>(b));			\
}																					\
constexpr inline EnumType operator&(EnumType a, EnumType b) noexcept {				\
	using U = std::underlying_type_t<EnumType>;										\
	return static_cast<EnumType>(static_cast<U>(a) & static_cast<U>(b));			\
}																					\
constexpr inline EnumType& operator|=(EnumType& a, EnumType b) noexcept {           \
	a = (a | b);                                                                    \
	return a;                                                                       \
}                                                                                   \
constexpr inline EnumType& operator&=(EnumType& a, EnumType b) noexcept {           \
	a = (a & b);                                                                    \
	return a;                                                                       \
}																					\
constexpr inline bool Any(EnumType v) noexcept {									\
	using U = std::underlying_type_t<EnumType>;										\
	return static_cast<U>(v) != 0;													\
}																					\
constexpr inline bool Test(EnumType v, EnumType t) noexcept {						\
	using U = std::underlying_type_t<EnumType>;										\
	return (static_cast<U>(v) & static_cast<U>(t)) != 0;							\
}
}