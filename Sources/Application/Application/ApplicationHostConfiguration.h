#pragma once

#include "Application/ApplicationLaunchOptions.h"
#include "AppRuntimeConfig.h"
#include "RuntimePaths.h"

#include <filesystem>

namespace gglab
{
	[[nodiscard]] AppRuntimeConfig TranslateApplicationLaunchOptions(
		const ApplicationLaunchOptions& options, AppRuntimeExtent initialExtent,
		bool requestRuntimeValidation) noexcept;
	[[nodiscard]] RuntimePaths BuildRuntimePaths(
		const std::filesystem::path& executableDirectory) noexcept;
}
