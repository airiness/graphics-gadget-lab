#pragma once
#include "Graphics/GIShaderConstants.h"

#include <algorithm>
#include <cstdint>

namespace gglab
{
	struct GIObservationTile
	{
		uint32_t m_X = 0;
		uint32_t m_Y = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		bool m_BeginSweep = false;
		bool m_EndSweep = false;
	};

	using gi_shader::GIQueryGroupWidth;
	using gi_shader::GIQueryGroupHeight;
	inline constexpr uint64_t GIQueryWorkPerDispatch = 1ull << 23;

	// Resolve-only frames still visit every pixel. Round down to whole workgroups,
	// retaining at least one group even when one row exceeds the dispatch budget.
	[[nodiscard]] inline uint32_t GIQueryRowsPerDispatch(uint32_t width,
		uint32_t samples, uint32_t queriesPerSample) noexcept
	{
		const uint64_t rowWork = uint64_t(width) * std::max(1ull, uint64_t(samples) * queriesPerSample);
		return std::max(GIQueryGroupHeight,
			static_cast<uint32_t>(GIQueryWorkPerDispatch / rowWork) /
				GIQueryGroupHeight * GIQueryGroupHeight);
	}
}
