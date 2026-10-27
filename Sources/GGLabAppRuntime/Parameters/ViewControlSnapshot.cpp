#include "Parameters/ViewControlSnapshot.h"
#include "Capture/FrameCaptureCoordinator.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] ViewControlFrameObservation ObserveFrame(const FrameCaptureFrameState& frame)
		{
			ViewControlFrameObservation result{
				.m_Control = frame.m_HostViewControl,
				.m_FrameSerial = frame.m_FrameSerial,
				.m_DisplayViewId = frame.m_SettleKey.m_Temporal.m_Identity.m_DisplayViewId,
				.m_Values = frame.m_HostViewProjection,
				.m_Consumers = frame.m_Temporal.m_Consumers,
			};
			switch (frame.m_Outcome)
			{
			case FrameCaptureFrameOutcome::Planned:
				result.m_Outcome = ViewControlFrameOutcome::Planned;
				break;
			case FrameCaptureFrameOutcome::Submitted:
				result.m_Outcome = ViewControlFrameOutcome::Submitted;
				break;
			case FrameCaptureFrameOutcome::Aborted:
				result.m_Outcome = ViewControlFrameOutcome::Aborted;
				break;
			}
			return result;
		}
	}

	ViewControlSnapshot BuildViewControlSnapshot(
		const ApplicationViewSettingsResolution& inherited,
		std::span<const LabParameterValue> overrides,
		const FrameCaptureFrameState& planned, const FrameCaptureFrameState* previous)
	{
		auto hostView = inherited.m_View;
		ApplyHostViewParameters(overrides, hostView, inherited.m_Profile.m_EnableScenePreExposure);
		ViewControlSnapshot result{
			.m_Control = planned.m_HostViewControl,
			.m_Inherited = DescribeHostViewParameters(inherited.m_View),
			.m_HostView = DescribeHostViewProjection(hostView, inherited.m_ToolingBlocks, overrides),
			.m_PlannedFrame = ObserveFrame(planned),
		};
		if (previous)
		{
			result.m_PreviousFrame = ObserveFrame(*previous);
		}
		return result;
	}
}
