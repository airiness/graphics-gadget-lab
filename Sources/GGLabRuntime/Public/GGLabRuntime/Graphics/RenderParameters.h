#pragma once

#include <cstdint>
#include <type_traits>

namespace gglab
{
	inline constexpr uint32_t MaxDrawConstantDWORDs = 4;
	inline constexpr uint32_t MaxInlineConstantDWORDs = 8;
	// Fits the minimum Vulkan uniform-buffer range and the DX12 CBV range.
	inline constexpr uint32_t MaxPassConstantBufferBytes = 16 * 1024;

	// CPU semantic object for data that changes at draw-call frequency.
	// The common root signature binds this object as DrawConstants.
	struct DrawParameters
	{
		uint32_t ObjectOffset = 0;

		bool operator==(const DrawParameters&) const noexcept = default;
	};

	template <typename T>
	inline constexpr bool IsShaderParameterStruct =
		std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> &&
		(sizeof(T) % sizeof(uint32_t) == 0);

	template <typename T>
	inline constexpr bool IsInlineConstantStruct =
		IsShaderParameterStruct<T> && (sizeof(T) <= MaxInlineConstantDWORDs * sizeof(uint32_t));

	template <typename T>
	inline constexpr bool IsPassConstantBufferStruct =
		IsShaderParameterStruct<T> && (sizeof(T) <= MaxPassConstantBufferBytes);

	static_assert(IsShaderParameterStruct<DrawParameters>);
	static_assert(sizeof(DrawParameters) == 4);
	static_assert(sizeof(DrawParameters) <= MaxDrawConstantDWORDs * sizeof(uint32_t));
}
