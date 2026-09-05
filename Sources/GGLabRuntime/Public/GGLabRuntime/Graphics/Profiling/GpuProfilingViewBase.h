#pragma once

#include "GGLabRuntime/Graphics/Profiling/GpuProfileFrameSnapshot.h"

namespace gglab
{
	// Non-owning observation capability; the backend profiler owns publication.
	class GpuProfilingViewBase
	{
	public:
		virtual ~GpuProfilingViewBase() = default;

		// Reports the accepted enable request, not whether the current frame is
		// recording. Unsupported backends may reject an enable request.
		[[nodiscard]] virtual bool IsEnabled() const noexcept = 0;
		// May be invalid before the first completed result, or retain the last
		// completed result while profiling is disabled. Never waits for the GPU.
		[[nodiscard]] virtual GpuProfileFrameSnapshot GetLatestFrame() const = 0;
	};
}
