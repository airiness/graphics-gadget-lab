#pragma once
#include "Application/ApplicationLaunchOptions.h"

#include <windows.h>

namespace gglab
{
	// Runs the non-renderer startup path for explicit Vulkan inspection modes.
	// --list-adapters enumerates and evaluates every adapter before exiting;
	// regular --rhi selection is handled by the Renderer/RHIContext factory.
	[[nodiscard]] int RunRenderingStartupPath(
		const ApplicationLaunchOptions& options, HWND hwnd) noexcept;
}
