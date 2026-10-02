#pragma once

namespace gglab
{
	class SelfTestContext;

	void RunBrdfLutReferenceSelfTests(SelfTestContext& context) noexcept;
	void RunDX12BrdfLutNumericSelfTests(SelfTestContext& context) noexcept;
	void RunVulkanBrdfLutNumericSelfTests(SelfTestContext& context) noexcept;
}
