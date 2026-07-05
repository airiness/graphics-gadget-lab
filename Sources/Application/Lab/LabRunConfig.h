#pragma once

#include <algorithm>
#include <cstdint>

namespace gglab
{
	struct LabRunConfig
	{
		uint64_t m_RandomSeed = 0;
		uint32_t m_WarmupFrames = 8;
		bool m_UseFixedDeltaTime = false;
		float m_FixedDeltaTime = 1.0f / 60.0f;

		void Sanitize() noexcept
		{
			m_WarmupFrames = std::min(m_WarmupFrames, 10000u);
			m_FixedDeltaTime = std::clamp(m_FixedDeltaTime, 1.0f / 1000.0f, 1.0f);
		}
	};
}
