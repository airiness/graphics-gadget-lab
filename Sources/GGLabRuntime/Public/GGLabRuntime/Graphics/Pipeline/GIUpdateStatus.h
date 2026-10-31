#pragma once
#include <cstdint>

namespace gglab
{
	enum class GIUpdateError : uint32_t
	{
		None = 0,
		InvalidRay = 1,
		InvalidHistory = 2,
		InvalidBlend = 3,
	};

	// First numerical failure in a field generation. Element is a ray index for
	// InvalidRay, otherwise an interior atlas texel. Update is zero-based.
	struct GIUpdateStatus
	{
		GIUpdateError m_Error = GIUpdateError::None;
		uint32_t m_Probe = UINT32_MAX;
		uint32_t m_Update = UINT32_MAX;
		uint32_t m_Element = UINT32_MAX;
	};
	static_assert(sizeof(GIUpdateStatus) == 16);
}
