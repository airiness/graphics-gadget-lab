#pragma once

#include "GGLabRuntime/Graphics/Profiling/GpuProfilingControlBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingViewBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfileFrameSnapshot.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>

namespace gglab
{
	// Shared by native profilers: a same-name non-pass scope must never merge
	// with a graph pass, nor may distinct declaration occurrences merge.
	inline void AccumulateGpuProfileSample(
		GpuProfileFrameSnapshot& frame, GpuProfileScope scope, double milliseconds) noexcept
	{
		auto sample = std::ranges::find_if(frame.m_Samples,
			[scope](const GpuProfileSample& candidate) { return candidate.GetScope() == scope; });
		if (sample == frame.m_Samples.end())
		{
			frame.m_Samples.push_back({ .m_Name = std::string(scope.m_Name),
				.m_Kind = scope.m_Kind, .m_Occurrence = scope.m_Occurrence });
			sample = std::prev(frame.m_Samples.end());
		}
		sample->m_Milliseconds += milliseconds;
		++sample->m_CallCount;
	}

	// Runtime composition may bind both capabilities to one backend owner.
	// Ordinary tooling receives the Public query/control interfaces separately.
	class GpuProfiler : public GpuProfilingViewBase, public GpuProfilingControlBase
	{
	public:
		~GpuProfiler() override = default;
		// Bind the active recording, if any, before it is ended or submitted.
		virtual void SetRenderFrameSerial(uint64_t frameSerial) noexcept = 0;
	};
}
