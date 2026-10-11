#pragma once
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"

#include <cstdint>
#include <vector>

namespace gglab
{
	// Application-facing frame capture control. Calls are made from the thread
	// that drives the render host frames.
	class FrameCaptureControlBase
	{
	public:
		virtual ~FrameCaptureControlBase() = default;

		// Queues a capture of the next frame that records the source tap and
		// returns its non-zero request id. A frame aborted before submission
		// leaves the request queued; every request finishes with exactly one
		// result, at the latest when the render host finalizes.
		[[nodiscard]] virtual uint64_t RequestCapture(FrameCaptureSource source) noexcept = 0;
		// Queues a Diagnostic capture of the tap, with the same lifecycle as
		// RequestCapture. A tap without a diagnostic name fails immediately.
		[[nodiscard]] virtual uint64_t RequestDiagnosticCapture(
			PostProcessDebugTap tap) noexcept = 0;
		// Appends finished results in completion order and clears them from the
		// control.
		virtual void ConsumeResults(std::vector<FrameCaptureResult>& outResults) noexcept = 0;
		// Requests that have not produced a result yet, including queued ones.
		[[nodiscard]] virtual uint32_t GetUnfinishedRequestCount() const noexcept = 0;
	};
}
