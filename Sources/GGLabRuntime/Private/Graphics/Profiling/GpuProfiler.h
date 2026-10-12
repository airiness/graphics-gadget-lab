#pragma once

#include "GGLabRuntime/Graphics/Profiling/GpuProfilingControlBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingViewBase.h"

#include <cstdint>

namespace gglab
{
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
