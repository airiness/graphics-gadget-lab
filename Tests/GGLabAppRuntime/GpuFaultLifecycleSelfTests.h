#pragma once
#include "GGLabTestCore/SelfTest.h"

#include <filesystem>
#include <string_view>

namespace gglab
{
	void RunGpuFaultLifecycleSelfTests(SelfTestContext& context) noexcept;
	int RunServicePublicationChild(std::string_view scenario) noexcept;
	int RunGpuFaultLifecycleChild(std::string_view mode,
		const std::filesystem::path& receipt) noexcept;
}
