#pragma once

namespace gglab
{
	class SelfTestContext;

	void RunDX12TemporalAccumulationNumericSelfTests(SelfTestContext& context) noexcept;
	void RunVulkanTemporalAccumulationNumericSelfTests(SelfTestContext& context) noexcept;
}
