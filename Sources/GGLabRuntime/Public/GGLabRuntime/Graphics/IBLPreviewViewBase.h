#pragma once

#include "GGLabRuntime/Graphics/IBLPreviewDiagnostics.h"

namespace gglab
{
	// Borrowed for the render-thread tooling draw. Copies describe current requested
	// selection and active allocations, not staging resources or completed GPU work.
	// Descriptor copies do not pin resources and may be sampled only in this draw
	// through the GUI adapter and existing RenderGraph ordering. Never cache them
	// across frames, replacement, retirement or shutdown.
	class IBLPreviewViewBase
	{
	public:
		virtual ~IBLPreviewViewBase() = default;
		[[nodiscard]] virtual IBLPreviewResourcesDiagnostics GetIBLPreviewResourcesDiagnostics()
			const noexcept = 0;
	};
}
