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

	struct GpuProfileFrameSnapshot
	{
		uint64_t m_FrameIndex = 0;
		double m_FrameMilliseconds = 0.0;
		std::vector<GpuProfileSample> m_Samples;

		[[nodiscard]] bool IsValid() const noexcept { return m_FrameIndex != 0; }
	};

	class GpuProfiler
	{
	public:
		virtual ~GpuProfiler() = default;

		virtual void SetEnabled(bool enabled) noexcept = 0;
		[[nodiscard]] virtual bool IsEnabled() const noexcept = 0;
		[[nodiscard]] virtual GpuProfileFrameSnapshot GetLatestFrame() const = 0;
	};
}
