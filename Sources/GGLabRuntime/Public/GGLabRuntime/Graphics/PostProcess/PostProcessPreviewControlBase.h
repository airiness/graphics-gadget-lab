#pragma once

#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"

namespace gglab
{
	// Borrowed, render-thread-only controls for requested CPU state. During
	// tooling draw the current graph is already built: changes are consumed by
	// a later graph build, without modifying its captured selection or exposure.
	// Allocation, publication, request consumption and retirement remain private.
	class PostProcessPreviewControlBase
	{
	public:
		virtual ~PostProcessPreviewControlBase() = default;

		// Invalid taps are ignored; bloom levels are clamped to the supported range.
		virtual void SetPostProcessPreviewSelection(PostProcessDebugSelection selection)
			noexcept = 0;
		virtual void SetPostProcessPreviewExposureEV(float exposureEV) noexcept = 0;
		// Idempotent request for a later graph build; setters alone do not request work.
		virtual void RequestPostProcessPreview() noexcept = 0;
	};
}
