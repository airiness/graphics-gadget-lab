#pragma once

#include "Application/ApplicationLaunchOptions.h"
#include "AppRuntimeConfig.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "RuntimePaths.h"

#include <filesystem>
#include <optional>

namespace gglab
{
	[[nodiscard]] AppRuntimeConfig TranslateApplicationLaunchOptions(
		const ApplicationLaunchOptions& options, AppRuntimeExtent initialExtent,
		bool requestRuntimeValidation) noexcept;
	// Unattended hidden launches advance time by this step unless one is specified.
	inline constexpr double HiddenLaunchFixedDeltaTimeSeconds = 1.0 / 60.0;

	[[nodiscard]] std::optional<FrameCaptureRequest> TranslateCaptureOnReadyOptions(
		const ApplicationLaunchOptions& options) noexcept;
	[[nodiscard]] RuntimePaths BuildRuntimePaths(
		const std::filesystem::path& executableDirectory,
		const std::filesystem::path& stateDirectory = {}) noexcept;
}
