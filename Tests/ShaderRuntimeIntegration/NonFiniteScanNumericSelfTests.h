#pragma once

namespace gglab
{
	class SelfTestContext;
	void RunDX12NonFiniteScanNumericSelfTests(SelfTestContext& context) noexcept;
	void RunVulkanNonFiniteScanNumericSelfTests(SelfTestContext& context) noexcept;
}
