#pragma once
#include "Application/ApplicationLaunchOptions.h"

#include <windows.h>

namespace gglab
{
	// Runs the non-renderer startup path for backend-selection CLI modes:
	// --list-adapters enumerates and evaluates every Vulkan adapter, and an
	// explicit --rhi vulkan creates the instance, surface, adapter, device
	// and queue qualification before exiting. Returns the process exit code;
	// an explicit Vulkan selection never falls back to DX12.
	[[nodiscard]] int RunRenderingStartupPath(
		const ApplicationLaunchOptions& options, HWND hwnd) noexcept;
}
