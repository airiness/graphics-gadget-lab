#pragma once
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <string_view>

namespace gglab
{
	inline constexpr std::string_view AllApplicationSelfTestsSelection = "all";
	inline constexpr std::string_view ApplicationPathCompositionSelfTestSelection =
		"app-path-composition";
	inline constexpr std::string_view ApplicationArtifactPackageClosureSelfTestSelection =
		"artifact-package-closure";

	struct RuntimePaths;

	[[nodiscard]] bool IsApplicationSelfTestSelectionValid(std::string_view selection) noexcept;
	[[nodiscard]] bool RunApplicationSelfTests(std::string_view selection) noexcept;
	[[nodiscard]] bool RunApplicationPathCompositionSelfTest(
		const RuntimePaths& runtimePaths, RHIBackendType backend) noexcept;
	[[nodiscard]] bool RunApplicationArtifactPackageClosureSelfTest(
		const RuntimePaths& runtimePaths, RHIBackendType backend) noexcept;
}
