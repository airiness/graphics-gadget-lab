#pragma once
#include "GGLabTestCore/SelfTest.h"

#include <filesystem>
#include <string_view>

namespace gglab
{
	void RunGpuFaultLifecycleSelfTests(SelfTestContext& context) noexcept;
	int RunGpuFaultLifecycleChild(std::string_view mode,
		const std::filesystem::path& receipt) noexcept;
}
