#pragma once

namespace gglab
{
	class SelfTestContext;
	void RunDX12LayoutBindingNumericSelfTests(SelfTestContext& context) noexcept;
	void RunVulkanLayoutBindingNumericSelfTests(SelfTestContext& context) noexcept;
}
