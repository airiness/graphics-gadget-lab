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
	void RunCaptureLifecycleIntegrationSelfTests(SelfTestContext& context) noexcept;
	void RunContentLifetimeIntegrationSelfTests(SelfTestContext& context) noexcept;
	void RunHostViewIntegrationSelfTests(SelfTestContext& context) noexcept;
	void RunLightingParameterIntegrationSelfTests(SelfTestContext& context) noexcept;
	void RunParameterRequestIntegrationSelfTests(SelfTestContext& context) noexcept;
	void RunSequenceParameterIntegrationSelfTests(SelfTestContext& context) noexcept;
	void RunFrameSettlementIntegrationSelfTests(SelfTestContext& context) noexcept;
	void RunGpuTeardownIntegrationSelfTests(SelfTestContext& context) noexcept;
}
