#pragma once

#include "GGLabRuntime/Graphics/GraphicsTypes.h"

#include <optional>

namespace gglab
{
	// Synchronous observations of camera routing during tooling draw. No camera or slot escapes.
	class CameraRenderViewQueryBase
	{
	public:
		virtual ~CameraRenderViewQueryBase() = default;
		[[nodiscard]] virtual RenderViewID GetDisplayViewId() const noexcept = 0;
		[[nodiscard]] virtual std::optional<RenderViewVisibilityMode> GetRenderViewVisibilityMode(
			RenderViewID viewId) const noexcept = 0;
	};
}
