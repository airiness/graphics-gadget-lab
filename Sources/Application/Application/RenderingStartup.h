#pragma once
#include "Application/ApplicationLaunchOptions.h"

#include <windows.h>

namespace gglab
{
	// Runs explicit Vulkan inspection and hardware qualification exit modes.
	// Regular --rhi selection is handled by the Renderer/RHIContext factory.
	[[nodiscard]] int RunRenderingStartupPath(
		const ApplicationLaunchOptions& options, HWND hwnd) noexcept;
}
