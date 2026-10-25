#pragma once
#include "GGLabTestCore/SelfTest.h"

namespace gglab
{
	void RunRenderingContractSelfTests(SelfTestContext& context) noexcept;
	// Private CPU measurement mode; graph declaration/destruction, without GPU execution.
	void RunRenderGraphObjectStorageBenchmark() noexcept;
}
