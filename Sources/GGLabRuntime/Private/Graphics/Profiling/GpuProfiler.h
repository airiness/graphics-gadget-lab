#pragma once

#include "GGLabRuntime/Graphics/Profiling/GpuProfilingControlBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingViewBase.h"

namespace gglab
{
	// Runtime composition may bind both capabilities to one backend owner.
	// Ordinary tooling receives the Public query/control interfaces separately.
	class GpuProfiler : public GpuProfilingViewBase, public GpuProfilingControlBase
	{
	public:
		~GpuProfiler() override = default;
	};
}
