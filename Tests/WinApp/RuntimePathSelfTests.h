#pragma once
#include "GGLabTestCore/SelfTest.h"

#include <filesystem>

namespace gglab
{
	void RunApplicationPathCompositionSelfTests(SelfTestContext& context,
		const std::filesystem::path& application) noexcept;
	void RunApplicationArtifactPackageClosureSelfTests(SelfTestContext& context,
		const std::filesystem::path& application) noexcept;
}
