#pragma once

#include "GGLabRuntime/Graphics/IBLPreviewTypes.h"

#include <cstdint>

namespace gglab
{
	// Render-thread controls for requested preview state. Tooling runs after graph
	// construction; requests are consumed by a later graph build. They never allocate,
	// publish, retire or modify inputs already copied into a render pass.
	class IBLPreviewControlBase
	{
	public:
		virtual ~IBLPreviewControlBase() = default;
		// Invalid layouts are ignored. Environment layout requests always mark dirty;
		// irradiance and specular do so only when their accepted layout changes.
		virtual void SetIBLEnvironmentPreviewLayout(IBLPreviewLayout layout) noexcept = 0;
		virtual void SetIBLIrradiancePreviewLayout(IBLPreviewLayout layout) noexcept = 0;
		virtual void SetIBLPrefilteredSpecularPreviewLayout(IBLPreviewLayout layout) noexcept = 0;
		// Preserve the requested mip; the consuming pass clamps to its source extent.
		virtual void SetIBLEnvironmentPreviewMip(uint32_t mip) noexcept = 0;
		virtual void SetIBLPrefilteredSpecularPreviewMip(uint32_t mip) noexcept = 0;
		// Coalesces requests per preview. Invalid types are ignored.
		virtual void RequestIBLPreview(IBLPreviewType type) noexcept = 0;
	};
}
