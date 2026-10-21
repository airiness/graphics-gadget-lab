#pragma once

#include "GGLabRuntime/Graphics/Profiling/GpuProfilingControlBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingViewBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfileFrameSnapshot.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <string>

namespace gglab
{
	// One atomic value orders enable changes with frame recording. Equal requests
	// do not create a measurement change; every actual toggle changes the value.
	class GpuProfilingState
	{
	public:
		void RequestEnabled(bool enabled) noexcept
		{
			uint64_t state = m_State.load(std::memory_order_relaxed);
			while (bool(state & 1) != enabled)
			{
				if (m_State.compare_exchange_weak(state, state + 1, std::memory_order_relaxed))
				{
					return;
				}
			}
		}
		[[nodiscard]] uint64_t Read() const noexcept { return m_State.load(std::memory_order_relaxed); }
		[[nodiscard]] bool IsEnabled() const noexcept { return bool(Read() & 1); }

	private:
		std::atomic_uint64_t m_State{ 1 };
	};

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
