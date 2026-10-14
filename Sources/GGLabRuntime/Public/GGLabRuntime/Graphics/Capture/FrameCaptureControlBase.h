#pragma once
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gglab
{
	class RenderGraph;
	// Application-facing frame capture control. Calls are made from the thread
	// that drives the render host frames.
	class FrameCaptureControlBase
	{
	public:
		virtual ~FrameCaptureControlBase() = default;

		// Queues a capture of the next frame that records the source tap and
		// returns its non-zero request id, or zero without queuing a request/result
		// when the bounded request slots (including unconsumed results) are full.
		// A zero frameSerial allows retry after an aborted frame. A non-zero serial
		// restricts the request to that live, begun render frame: abort, submission
		// failure or a missing tap fails it instead of recording a later frame.
		// Every accepted request finishes with exactly one result.
		[[nodiscard]] virtual uint64_t RequestCapture(FrameCaptureSource source,
			uint64_t frameSerial = 0) noexcept = 0;
		// Queues a Diagnostic capture of the tap, with the same lifecycle as
		// RequestCapture. A tap without a diagnostic name fails immediately.
		[[nodiscard]] virtual uint64_t RequestDiagnosticCapture(
			PostProcessDebugTap tap, uint64_t frameSerial = 0) noexcept = 0;
		[[nodiscard]] virtual uint64_t RequestResourceExport(std::vector<RGResourceAddress>,
			uint64_t = 0, bool = false, bool = false) noexcept
		{
			return 0;
		}
		// Sizes are known only after compilation. Start a scheduled export frame
		// with an idle payload pool, then check the complete group before submission.
		[[nodiscard]] virtual FrameCaptureAvailability GetResourceExportFrameAvailability() const noexcept
		{
			return GetRequestAvailability();
		}
		// Called after ordinary compilation, before execution. Busy defers the entire
		// selected sequence frame; no original producer is revived by these reads.
		[[nodiscard]] virtual FrameCaptureAvailability PrepareResourceExports(uint64_t,
			RenderGraph&, ResourceExportFrameContext = {}) noexcept
		{
			return FrameCaptureAvailability::Available;
		}
		virtual void DeferFrameCaptures(uint64_t) noexcept {}
		// Tap of the oldest queued Diagnostic request. Scheduling can coalesce
		// matching taps or wait before fixing metadata for a different tap.
		[[nodiscard]] virtual std::optional<PostProcessDebugTap> GetPendingDiagnosticTap()
			const noexcept = 0;
		// Request-only admission, including unconsumed results and the service's
		// configured limit. Graph captures do not need an image payload reservation.
		[[nodiscard]] virtual FrameCaptureAvailability GetRequestAvailability() const noexcept = 0;
		// Checks request and payload capacity without reserving it. The tap rechecks
		// its actual target before allocation; capacity exhaustion fails that frame's
		// request rather than silently capturing a later frame. Retained CPU images
		// keep their payload capacity until their last consumer releases them.
		[[nodiscard]] virtual FrameCaptureAvailability GetAvailability(
			RHIFormat format, uint32_t width, uint32_t height) const noexcept = 0;
		// Polls GPU completion even when no new frame is rendered, then appends
		// finished results in completion order and clears them from the
		// control.
		virtual void ConsumeResults(std::vector<FrameCaptureResult>& outResults) noexcept = 0;
		// Requests that have not produced a result yet, including queued ones.
		[[nodiscard]] virtual uint32_t GetUnfinishedRequestCount() const noexcept = 0;
	};
}
