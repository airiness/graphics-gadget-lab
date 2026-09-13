#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gglab
{
	struct GpuProfileSample
	{
		std::string m_Name;
		double m_Milliseconds = 0.0;
		uint32_t m_CallCount = 0;
	};

	// An owned copy of the latest completed GPU timing publication. Retaining it
	// does not retain backend resources or depend on a live diagnostics frame.
	struct GpuProfileFrameSnapshot
	{
		uint64_t m_FrameIndex = 0;
		double m_FrameMilliseconds = 0.0;
		std::vector<GpuProfileSample> m_Samples;

		[[nodiscard]] bool IsValid() const noexcept { return m_FrameIndex != 0; }
	};
}
