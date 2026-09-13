#pragma once
#include "Application/ApplicationLaunchOptions.h"

#include <windows.h>

namespace gglab
{
	// Runs the explicit Vulkan adapter-inspection exit mode.
	// Regular --rhi selection is handled by the Renderer/RHIContext factory.
	[[nodiscard]] int RunRenderingStartupPath(
		const ApplicationLaunchOptions& options, HINSTANCE instance,
		bool requestValidation) noexcept;
}
