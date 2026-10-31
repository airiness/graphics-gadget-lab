#pragma once

#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gglab::ray_scene_bindings
{
	// t5-t10 in the ray-scene shaders. Slots describe semantics, not native root indices.
	enum Slot : uint32_t
	{
		TopLevel,
		Vertices,
		Indices,
		Geometries,
		Instances,
		Materials,
		Count,
	};
	inline constexpr uint32_t GeometryOnlyCount = Materials;
	inline constexpr uint32_t FirstShaderRegister = 5;
	inline constexpr uint32_t BindingKeyBase = 0x700;

	[[nodiscard]] inline LayoutBindingKey Key(uint32_t slot) noexcept
	{
		return LayoutBindingKey{ BindingKeyBase + slot };
	}

	[[nodiscard]] inline uint32_t NextParameterOrder(
		std::span<const LayoutBindingRequirement> bindings) noexcept
	{
		uint32_t next = 0;
		for (const auto& binding : bindings)
		{
			next = std::max(next, binding.m_ParameterOrder + 1);
		}
		return next;
	}

	inline void Append(std::vector<LayoutBindingRequirement>& bindings, uint32_t count = Count)
	{
		const auto first = NextParameterOrder(bindings);
		for (uint32_t slot = 0; slot < count; ++slot)
		{
			bindings.push_back({ .m_Key = Key(slot),
				.m_Slot = { .m_Type = slot == TopLevel ?
						RHIBindingType::AccelerationStructure :
						RHIBindingType::ReadOnlyStorageBuffer,
					.m_Visibility = RHIShaderStage::Compute,
					.m_Binding = FirstShaderRegister + slot, .m_Count = 1 },
				.m_ParameterOrder = first + slot });
		}
	}
}
