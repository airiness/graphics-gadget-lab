#pragma once

namespace gglab
{
	class SelfTestContext;

	void RunFrameCaptureCoordinatorSelfTests(SelfTestContext& context) noexcept;
	void RunFrameCaptureMetadataSelfTests(SelfTestContext& context) noexcept;
	void RunGIFieldLightingRefreshSelfTests(SelfTestContext& context) noexcept;
	void RunGIProbeInspectionSelfTests(SelfTestContext& context) noexcept;
	void RunCaptureBundlesSelfTests(SelfTestContext& context) noexcept;
	void RunCaptureInspectionSelfTests(SelfTestContext& context) noexcept;
	void RunCaptureJobsSelfTests(SelfTestContext& context) noexcept;
	void RunCapturePublicationSelfTests(SelfTestContext& context) noexcept;
	void RunCaptureResourcesSelfTests(SelfTestContext& context) noexcept;
	void RunCaptureSelectionSelfTests(SelfTestContext& context) noexcept;
	void RunCaptureSequenceSelfTests(SelfTestContext& context) noexcept;
	void RunCaptureTimingSelfTests(SelfTestContext& context) noexcept;
}
