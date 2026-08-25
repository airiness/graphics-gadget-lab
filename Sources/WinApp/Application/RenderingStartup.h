#pragma once
#include "Application/ApplicationLaunchOptions.h"
#include "RuntimePaths.h"

#include <windows.h>

namespace gglab
{
	// Runs explicit Vulkan inspection and hardware qualification exit modes.
	// Regular --rhi selection is handled by the Renderer/RHIContext factory.
	[[nodiscard]] int RunRenderingStartupPath(
		const ApplicationLaunchOptions& options, const RuntimePaths& runtimePaths,
		HINSTANCE instance, bool requestValidation) noexcept;
}
