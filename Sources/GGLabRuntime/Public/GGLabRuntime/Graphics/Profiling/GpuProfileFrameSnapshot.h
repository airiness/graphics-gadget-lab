#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	enum class GpuProfileScopeKind : uint8_t
	{
		Other,
		RenderGraphPass,
	};

	// Borrowed for recording. Pass occurrences are one-based declaration ordinals,
	// including culled declarations; other scopes use occurrence zero.
	struct GpuProfileScope
	{
		std::string_view m_Name;
		GpuProfileScopeKind m_Kind = GpuProfileScopeKind::Other;
		uint32_t m_Occurrence = 0;

		bool operator==(const GpuProfileScope&) const noexcept = default;
	};

	struct GpuProfileSample
	{
		std::string m_Name;
		double m_Milliseconds = 0.0;
		uint32_t m_CallCount = 0;
		GpuProfileScopeKind m_Kind = GpuProfileScopeKind::Other;
		uint32_t m_Occurrence = 0;

		[[nodiscard]] GpuProfileScope GetScope() const noexcept
		{
			return { m_Name, m_Kind, m_Occurrence };
		}
	};

	// An owned copy of the latest completed GPU timing publication. Retaining it
	// does not retain backend resources or depend on a live diagnostics frame.
	struct GpuProfileFrameSnapshot
	{
		uint64_t m_FrameIndex = 0;
		// RenderHost correlation, independent of the profiler's own frame counter.
		// Zero for recordings that were not bound to a RenderHost frame.
		uint64_t m_RenderFrameSerial = 0;
		double m_FrameMilliseconds = 0.0;
		std::vector<GpuProfileSample> m_Samples;
		// Frozen at recording completion, independent of later profiler controls.
		bool m_ProfilingStateChanged = false;

		[[nodiscard]] bool IsValid() const noexcept { return m_FrameIndex != 0; }
	};
}
