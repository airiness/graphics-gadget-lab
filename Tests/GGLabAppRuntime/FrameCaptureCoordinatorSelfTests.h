#pragma once

namespace gglab
{
	class SelfTestContext;

	void RunFrameCaptureCoordinatorSelfTests(SelfTestContext& context) noexcept;
	void RunGIFieldLightingRefreshSelfTests(SelfTestContext& context) noexcept;
	void RunGIProbeInspectionSelfTests(SelfTestContext& context) noexcept;
}
