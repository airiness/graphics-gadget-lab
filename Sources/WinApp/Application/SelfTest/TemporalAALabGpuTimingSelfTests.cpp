#include "Application/SelfTest/TemporalAALabGpuTimingSelfTests.h"

#include "Application/Lab/Sessions/TemporalAALabGpuTiming.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Demo/DemoTypes.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfileFrameSnapshot.h"
#include "GGLabTestCore/SelfTest.h"

#include <cstdint>
#include <limits>

namespace gglab
{
	namespace
	{
		DemoFrameFeedback MakeFeedback(uint64_t serial) noexcept
		{
			DemoFrameFeedback feedback{};
			feedback.m_RenderSceneStatus = RenderSceneBuildStatus::Ready;
			feedback.m_SubmittedFence = { RHIFenceHandle{ 1, 1 }, serial };
			feedback.m_RenderFrameSerial = serial;
			feedback.m_TemporalAASettings.m_Enabled = true;
			feedback.m_TemporalFramePlan.m_DisplayViewId = RenderViewID::Main;
			feedback.m_TemporalFramePlan.m_Consumers[static_cast<uint32_t>(TemporalConsumer::TemporalAA)] = {
				.m_Status = TemporalConsumerStatus::Active,
				.m_DisableReason = TemporalConsumerDisableReason::None, .m_Requested = true,
			};
			feedback.m_ViewResolution = ResolveNativeViewResolution({ 1280, 720 });
			return feedback;
		}

		GpuProfileFrameSnapshot MakeProfile(uint64_t renderSerial, double milliseconds = 1.0) noexcept
		{
			// Deliberately unrelated counters: only the explicit serial binds a profile.
			return { .m_FrameIndex = renderSerial + 1000, .m_RenderFrameSerial = renderSerial,
				.m_Samples = { { "PostProcess.TemporalAA", milliseconds, 1 } } };
		}

		void SubmitRange(TemporalAALabGpuTimingWindow& window, DemoFrameFeedback& feedback,
			uint64_t first, uint64_t last, uint32_t frameSlots = 3) noexcept
		{
			for (uint64_t serial = first; serial <= last; ++serial)
			{
				feedback.m_RenderFrameSerial = serial;
				window.OnFrameSubmitted(feedback, frameSlots, true);
			}
		}
	}

	void RunTemporalAALabGpuTimingSelfTests(SelfTestContext& context) noexcept
	{
		TemporalAALabGpuTimingWindow window;
		auto feedback = MakeFeedback(10);
		window.OnFrameSubmitted(feedback, 3, true);
		window.OnFrameSubmitted(feedback, 3, true);
		context.Check(window.GetSnapshot().m_WarmupFramesRemaining == 15,
			"Duplicate submission feedback does not consume another warmup frame");
		SubmitRange(window, feedback, 11, 25);
		context.Check(window.GetSnapshot().m_WarmupFramesRemaining == 0 &&
			window.GetSnapshot().m_FirstEligibleFrameSerial == 0 && !window.Capture(MakeProfile(25)),
			"All sixteen Native warmup submissions are excluded, including the last one");
		SubmitRange(window, feedback, 26, 27);
		GGLAB_UNUSED(window.Capture(MakeProfile(26, 0.5)));
		context.Check(window.GetSnapshot().m_FirstEligibleFrameSerial == 26 &&
			window.GetSnapshot().m_SampleCount == 1 && window.GetSnapshot().m_LastGpuFrameIndex == 1026,
			"The first post-warmup profile is accepted using its explicit RenderFrame serial");
		GGLAB_UNUSED(window.Capture(MakeProfile(26)));
		GGLAB_UNUSED(window.Capture(MakeProfile(28)));
		GGLAB_UNUSED(window.Capture(MakeProfile(0)));
		context.Check(window.GetSnapshot().m_SampleCount == 1,
			"Repeated, future and unbound profiles cannot enter the submitted window");
		GGLAB_UNUSED(window.Capture(MakeProfile(27, 1.5)));
		context.Check(window.GetSnapshot().m_SampleCount == 2 &&
			window.GetSnapshot().m_SumMilliseconds == 2.0 &&
			window.GetSnapshot().m_MinMilliseconds == 0.5 && window.GetSnapshot().m_MaxMilliseconds == 1.5,
			"The window accumulates only distinct completed profiles");

		feedback.m_TemporalAASettings.m_HistoryFilter = TemporalAAHistoryFilter::Bilinear;
		SubmitRange(window, feedback, 28, 44);
		GGLAB_UNUSED(window.Capture(MakeProfile(27)));
		GGLAB_UNUSED(window.Capture(MakeProfile(43)));
		context.Check(window.GetSnapshot().m_SampleCount == 0 &&
			window.GetSnapshot().m_FirstEligibleFrameSerial == 44,
			"A filter override resets timing and excludes delayed old-domain and warmup profiles");
		GGLAB_UNUSED(window.Capture(MakeProfile(44)));
		context.Check(window.GetSnapshot().m_SampleCount == 1 &&
			window.GetSnapshot().m_Domain->m_Settings.m_HistoryFilter == TemporalAAHistoryFilter::Bilinear,
			"Timing records the submitted filter rather than the Lab authoring filter");

		window.Reset();
		feedback = MakeFeedback(100);
		feedback.m_TemporalAASettings.m_ResolutionPreset = TemporalAAResolutionPreset::Quality;
		feedback.m_TemporalFramePlan.m_TemporalAA.m_ResolutionPreset = TemporalAAResolutionPreset::Quality;
		feedback.m_ViewResolution.m_Render = { 854, 480 };
		SubmitRange(window, feedback, 100, 135);
		GGLAB_UNUSED(window.Capture(MakeProfile(135)));
		context.Check(window.GetSnapshot().m_WarmupFrameCount == 36 &&
			window.GetSnapshot().m_SampleCount == 0 && window.GetSnapshot().m_FirstEligibleFrameSerial == 0,
			"Quality excludes two complete eighteen-phase jitter cycles");
		SubmitRange(window, feedback, 136, 136);
		GGLAB_UNUSED(window.Capture(MakeProfile(136)));
		context.Check(window.GetSnapshot().m_SampleCount == 1,
			"Quality begins sampling on its thirty-seventh submitted frame");

		window.Reset();
		feedback = MakeFeedback(200);
		feedback.m_TemporalAASettings.m_ResolutionPreset = TemporalAAResolutionPreset::Quality;
		feedback.m_TemporalAASettings.m_TextureLodBiasOffset = -1.0f;
		SubmitRange(window, feedback, 200, 215);
		feedback.m_TemporalAASettings.m_ResolutionPreset = TemporalAAResolutionPreset::Native;
		feedback.m_TemporalAASettings.m_TextureLodBiasOffset = 0.0f;
		SubmitRange(window, feedback, 216, 216);
		context.Check(window.GetSnapshot().m_FirstEligibleFrameSerial == 216 &&
			window.GetSnapshot().m_WarmupFrameCount == 16 &&
			window.GetSnapshot().m_Domain->m_Settings.m_ResolutionPreset == TemporalAAResolutionPreset::Native,
			"Equivalent effective Native fallback settings preserve the window despite request changes");
		SubmitRange(window, feedback, 217, 217, 40);
		context.Check(window.GetSnapshot().m_WarmupFrameCount == 40 &&
			window.GetSnapshot().m_FirstDomainFrameSerial == 217,
			"Changing frame-slot count resets timing and establishes the warmup floor");

		window.Reset();
		feedback = MakeFeedback(300);
		SubmitRange(window, feedback, 300, 316);
		GGLAB_UNUSED(window.Capture(MakeProfile(316)));
		feedback.m_TemporalFramePlan.m_ResetIdentity = 1;
		SubmitRange(window, feedback, 317, 317);
		context.Check(window.GetSnapshot().m_SampleCount == 0 &&
			window.GetSnapshot().m_FirstDomainFrameSerial == 317,
			"A camera cut identity starts a new timing window");
		feedback.m_TemporalFramePlan.m_SessionIdentity = 2;
		SubmitRange(window, feedback, 318, 318);
		context.Check(window.GetSnapshot().m_FirstDomainFrameSerial == 318,
			"A session identity change restarts warmup");
		feedback.m_TemporalFramePlan.m_DisplayViewId = RenderViewID::DebugCamera0;
		SubmitRange(window, feedback, 319, 319);
		context.Check(window.GetSnapshot().m_FirstDomainFrameSerial == 319,
			"A display-view switch restarts warmup");
		feedback.m_ViewResolution.m_Render.m_Width = 640;
		SubmitRange(window, feedback, 320, 320);
		context.Check(window.GetSnapshot().m_FirstDomainFrameSerial == 320,
			"A render-extent change resets timing even when display size is unchanged");
		feedback.m_ViewResolution.m_Display.m_Width = 1920;
		SubmitRange(window, feedback, 321, 321);
		context.Check(window.GetSnapshot().m_FirstDomainFrameSerial == 321,
			"A display-extent change also resets timing");
		SubmitRange(window, feedback, 323, 323);
		context.Check(window.GetSnapshot().m_FirstDomainFrameSerial == 323 &&
			window.GetSnapshot().m_WarmupFramesRemaining == 15,
			"A missing submitted RenderFrame restarts warmup after an aborted recording");

		window.OnFrameSubmitted(feedback, 3, false);
		context.Check(!window.GetSnapshot().m_Domain,
			"Disabled profiling clears retained measurements");
		SubmitRange(window, feedback, 324, 324);
		feedback.m_TemporalFramePlan.m_Consumers[0].m_Status = TemporalConsumerStatus::Disabled;
		SubmitRange(window, feedback, 325, 325);
		context.Check(!window.GetSnapshot().m_Domain,
			"Inactive Temporal AA clears timing even if history allocation remains alive");
		feedback = MakeFeedback(326);
		feedback.m_SubmittedFence.Reset();
		window.OnFrameSubmitted(feedback, 3, true);
		context.Check(!window.GetSnapshot().m_Domain,
			"A frame without successful GPU submission cannot establish a timing domain");

		feedback = MakeFeedback(400);
		SubmitRange(window, feedback, 400, 420);
		auto invalid = MakeProfile(416);
		invalid.m_Samples.clear();
		GGLAB_UNUSED(window.Capture(invalid));
		invalid = MakeProfile(417, std::numeric_limits<double>::quiet_NaN());
		GGLAB_UNUSED(window.Capture(invalid));
		invalid = MakeProfile(418, -1.0);
		GGLAB_UNUSED(window.Capture(invalid));
		context.Check(window.GetSnapshot().m_SampleCount == 0,
			"Missing scopes, non-finite durations and negative timings are excluded");
		auto multiple = MakeProfile(419, 0.25);
		multiple.m_Samples.push_back({ "PostProcess.TemporalAA", 0.75, 1 });
		multiple.m_Samples.push_back({ "PostProcess.Other", 99.0, 1 });
		GGLAB_UNUSED(window.Capture(multiple));
		context.Check(window.GetSnapshot().m_SampleCount == 1 && window.GetSnapshot().m_SumMilliseconds == 1.0,
			"Repeated named TAA scopes are summed without unrelated pass durations");

		window.Reset();
		feedback = MakeFeedback(500);
		SubmitRange(window, feedback, 500, 515);
		uint32_t completions = 0;
		for (uint64_t serial = 516; serial < 516 + TemporalAALabGpuTimingSampleTarget; ++serial)
		{
			SubmitRange(window, feedback, serial, serial);
			if (window.Capture(MakeProfile(serial)))
			{
				++completions;
			}
		}
		GGLAB_UNUSED(window.Capture(MakeProfile(635)));
		context.Check(completions == 1 && window.GetSnapshot().m_SampleCount == 120 &&
			window.GetSnapshot().m_FirstSampleFrameSerial == 516 && window.GetSnapshot().m_LastSampleFrameSerial == 635,
			"Exactly 120 completed profiles produce one completion with the measured serial range");
		feedback.m_TemporalAASettings.m_MaxHistoryFeedback = 0.5f;
		SubmitRange(window, feedback, 636, 636);
		context.Check(window.GetSnapshot().m_SampleCount == 0 && window.GetSnapshot().m_FirstDomainFrameSerial == 636,
			"A completed window still observes later effective settings changes and restarts");
	}
}
