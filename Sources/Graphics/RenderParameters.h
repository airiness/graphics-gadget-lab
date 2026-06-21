#pragma once

#include <cstdint>
#include <type_traits>

namespace gglab
{
	inline constexpr uint32_t MaxDrawConstantDWORDs = 4;
	inline constexpr uint32_t MaxPassConstantDWORDs = 16;

	// CPU semantic object for data that changes at draw-call frequency.
	// The common root signature binds this object as DrawConstants.
	struct DrawParameters
	{
		uint32_t ObjectOffset = 0;
	};

	template<typename T>
	inline constexpr bool IsShaderParameterStruct =
		std::is_trivially_copyable_v<T> &&
		std::is_standard_layout_v<T> &&
		(sizeof(T) % sizeof(uint32_t) == 0);

	template<typename T>
	inline constexpr bool IsPassRootConstantStruct =
		IsShaderParameterStruct<T> &&
		(sizeof(T) <= MaxPassConstantDWORDs * sizeof(uint32_t));

	static_assert(IsShaderParameterStruct<DrawParameters>);
	static_assert(sizeof(DrawParameters) == 4);
	static_assert(sizeof(DrawParameters) <= MaxDrawConstantDWORDs * sizeof(uint32_t));
}
