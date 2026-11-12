#pragma once
#include "GGLabTestCore/SelfTest.h"

namespace gglab
{
	void RunRenderingContractSelfTests(SelfTestContext& context) noexcept;
	// Private CPU measurement mode; graph declaration/destruction, without GPU execution.
	void RunRenderGraphObjectStorageBenchmark() noexcept;
	void RunRenderingBindingsSelfTests(SelfTestContext& context) noexcept;
	void RunRenderingDepthSelfTests(SelfTestContext& context) noexcept;
	void RunRenderingGraphSelfTests(SelfTestContext& context) noexcept;
	void RunRenderingLightingSelfTests(SelfTestContext& context) noexcept;
	void RunRenderingMaterialsSelfTests(SelfTestContext& context) noexcept;
	void RunRenderingPreviewsSelfTests(SelfTestContext& context) noexcept;
	void RunRenderingResourcesSelfTests(SelfTestContext& context) noexcept;
	void RunRenderingSceneSelfTests(SelfTestContext& context) noexcept;
	void RunRenderingServicesSelfTests(SelfTestContext& context) noexcept;
	void RunRenderingShadowsSelfTests(SelfTestContext& context) noexcept;
	void RunRenderingSubmissionSelfTests(SelfTestContext& context) noexcept;
	void RunRenderingTemporalAASelfTests(SelfTestContext& context) noexcept;
	void RunRenderingTemporalHistorySelfTests(SelfTestContext& context) noexcept;
	void RunRenderingUploadsSelfTests(SelfTestContext& context) noexcept;
}
