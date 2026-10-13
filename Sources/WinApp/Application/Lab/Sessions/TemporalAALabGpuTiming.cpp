#include "Application/Lab/Sessions/TemporalAALabGpuTiming.h"

#include "Demo/DemoTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfileFrameSnapshot.h"

#include <algorithm>
#include <cmath>

namespace gglab
{
	void TemporalAALabGpuTimingWindow::OnFrameSubmitted(const DemoFrameFeedback& feedback,
		uint32_t frameSlotCount, bool profilingEnabled) noexcept
	{
		const auto& plan = feedback.m_TemporalFramePlan;
		const auto& resolution = feedback.m_ViewResolution;
		if (!profilingEnabled || feedback.m_RenderFrameSerial == 0 ||
			feedback.m_RenderSceneStatus != RenderSceneBuildStatus::Ready ||
			!feedback.m_SubmittedFence.IsValid() ||
			!plan.IsConsumerActive(TemporalConsumer::TemporalAA) ||
			resolution.m_Render.m_Width == 0 || resolution.m_Render.m_Height == 0 ||
			resolution.m_Display.m_Width == 0 || resolution.m_Display.m_Height == 0)
		{
			Reset();
			return;
		}
		if (feedback.m_RenderFrameSerial <= m_State.m_LastSubmittedFrameSerial)
		{
			return;
		}

		TemporalAASettings effective = feedback.m_TemporalAASettings;
		effective.m_Enabled = true;
		effective.m_ResolutionPreset = plan.m_TemporalAA.m_ResolutionPreset;
		effective.m_HistoryAccumulation = plan.m_TemporalAA.m_HistoryAccumulation;
		effective.m_TextureLodBiasOffset = plan.m_TemporalAA.m_TextureLodBiasOffset;
		const TemporalAALabGpuTimingDomain domain{
			.m_Settings = effective, .m_Resolution = resolution,
			.m_DisplayViewId = plan.m_DisplayViewId,
			.m_ResetIdentity = plan.m_ResetIdentity, .m_SessionIdentity = plan.m_SessionIdentity,
			.m_FrameSlotCount = frameSlotCount,
		};
		if (!m_State.m_Domain || *m_State.m_Domain != domain ||
			feedback.m_RenderFrameSerial != m_State.m_LastSubmittedFrameSerial + 1)
		{
			Reset();
			m_State.m_Domain = domain;
			m_State.m_FirstDomainFrameSerial = feedback.m_RenderFrameSerial;
			m_State.m_WarmupFrameCount = std::max(frameSlotCount,
				2u * GetTemporalAAJitterSequenceLength(effective.m_ResolutionPreset));
			m_State.m_WarmupFramesRemaining = m_State.m_WarmupFrameCount;
		}
		m_State.m_LastSubmittedFrameSerial = feedback.m_RenderFrameSerial;
		if (m_State.m_WarmupFramesRemaining > 0)
		{
			--m_State.m_WarmupFramesRemaining;
		}
		else if (m_State.m_FirstEligibleFrameSerial == 0)
		{
			m_State.m_FirstEligibleFrameSerial = feedback.m_RenderFrameSerial;
		}
	}

	bool TemporalAALabGpuTimingWindow::Capture(const GpuProfileFrameSnapshot& profile) noexcept
	{
		if (!profile.IsValid() || !m_State.m_Domain || m_State.m_FirstEligibleFrameSerial == 0 ||
			profile.m_RenderFrameSerial < m_State.m_FirstEligibleFrameSerial ||
			profile.m_RenderFrameSerial > m_State.m_LastSubmittedFrameSerial ||
			profile.m_FrameIndex <= m_State.m_LastGpuFrameIndex ||
			profile.m_RenderFrameSerial <= m_State.m_LastSampleFrameSerial ||
			m_State.m_SampleCount >= TemporalAALabGpuTimingSampleTarget)
		{
			return false;
		}
		m_State.m_LastGpuFrameIndex = profile.m_FrameIndex;
		double milliseconds = 0.0;
		bool found = false;
		for (const auto& sample : profile.m_Samples)
		{
			if (sample.m_Name == "PostProcess.TemporalAA")
			{
				if (!std::isfinite(sample.m_Milliseconds) || sample.m_Milliseconds < 0.0)
				{
					return false;
				}
				milliseconds += sample.m_Milliseconds;
				found = true;
			}
		}
		if (!found || !std::isfinite(milliseconds))
		{
			return false;
		}
		if (m_State.m_SampleCount == 0)
		{
			m_State.m_FirstSampleFrameSerial = profile.m_RenderFrameSerial;
			m_State.m_MinMilliseconds = milliseconds;
			m_State.m_MaxMilliseconds = milliseconds;
		}
		else
		{
			m_State.m_MinMilliseconds = std::min(m_State.m_MinMilliseconds, milliseconds);
			m_State.m_MaxMilliseconds = std::max(m_State.m_MaxMilliseconds, milliseconds);
		}
		m_State.m_LastSampleFrameSerial = profile.m_RenderFrameSerial;
		m_State.m_SumMilliseconds += milliseconds;
		++m_State.m_SampleCount;
		return m_State.m_SampleCount == TemporalAALabGpuTimingSampleTarget;
	}
}
