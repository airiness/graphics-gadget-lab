#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"

#include <Windows.h>

namespace gglab
{
	// Bootstrap consumes the surface factory synchronously; the host retains the window.
	// ABI suitability is supplied by host composition, before Vulkan loader access.
	[[nodiscard]] std::unique_ptr<RHIContext> CreateVulkanWin32Context(
		const RHIContextDesc& desc, HINSTANCE instance, HWND window, bool isHostAbiSupported) noexcept;
}
