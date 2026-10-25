#pragma once
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabTestCore/SelfTest.h"

namespace gglab
{
	struct RuntimePaths;

	void RunApplicationPathCompositionSelfTests(SelfTestContext& context,
		const RuntimePaths& runtimePaths, RHIBackendType backend) noexcept;
	void RunApplicationArtifactPackageClosureSelfTests(SelfTestContext& context,
		const RuntimePaths& runtimePaths, RHIBackendType backend) noexcept;
}
