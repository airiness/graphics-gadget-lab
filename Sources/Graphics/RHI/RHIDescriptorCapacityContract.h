#pragma once

#include <cstdint>

namespace gglab
{
	struct RHIDescriptorCapacityContract
	{
		uint32_t m_ResourceDescriptorCount = 0;
		uint32_t m_SamplerDescriptorCount = 0;
	};

	inline constexpr RHIDescriptorCapacityContract GGLabDescriptorCapacityContract{
		.m_ResourceDescriptorCount = 65'536,
		.m_SamplerDescriptorCount = 2'048,
	};
}
