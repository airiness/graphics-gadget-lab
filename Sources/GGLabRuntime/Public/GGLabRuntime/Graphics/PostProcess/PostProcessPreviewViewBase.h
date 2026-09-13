#pragma once

#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewDiagnostics.h"

namespace gglab
{
	// Borrowed, render-thread-only query. Observes the current requested state
	// separately from the last recorded preview; publication is not a GPU wait.
	class PostProcessPreviewViewBase
	{
	public:
		virtual ~PostProcessPreviewViewBase() = default;

		[[nodiscard]] virtual PostProcessPreviewDiagnostics GetPostProcessPreviewDiagnostics()
			const noexcept = 0;
	};
}
