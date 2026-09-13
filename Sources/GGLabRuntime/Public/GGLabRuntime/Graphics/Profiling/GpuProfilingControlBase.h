#pragma once

namespace gglab
{
	// Non-owning request capability, separate from timing observation.
	class GpuProfilingControlBase
	{
	public:
		virtual ~GpuProfilingControlBase() = default;

		// Updates the requested state only. The backend samples it at its next
		// BeginFrame; an active frame completes or aborts with its recording state
		// unchanged. Repeated requests before that point use the latest value.
		virtual void RequestEnabled(bool enabled) noexcept = 0;
	};
}
