#pragma once
#include "ApplicationToolingIntegration.h"
#include "Lab/LabParameter.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Parameters/HostViewParameters.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gglab
{
	struct FrameCaptureFrameState;

	enum class ViewControlFrameOutcome : uint8_t
	{
		Planned,
		Submitted,
		Aborted,
	};

	// Owned display-view evidence. Submitted means CPU-observed submission, not
	// GPU completion or proof that a particular graph pass executed. Aborted is a
	// capture outcome, not a guarantee that no GPU work was queued.
	struct ViewControlFrameObservation
	{
		HostViewControlStamp m_Control;
		uint64_t m_FrameSerial = 0;
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		ViewControlFrameOutcome m_Outcome = ViewControlFrameOutcome::Planned;
		std::vector<HostViewProjectionEntry> m_Values;
		std::vector<FrameCaptureTemporalConsumer> m_Consumers;
	};

	// Captured before GUI callbacks. Tooling drafts edited during Draw are not
	// part of this snapshot and cannot change either frame's frozen observation.
	struct ViewControlSnapshot
	{
		HostViewControlStamp m_Control;
		LabParameterSet m_Inherited;
		// After Host overrides, before sequence/reference masks.
		std::vector<HostViewProjectionEntry> m_HostView;
		ViewControlFrameObservation m_PlannedFrame;
		// Carries its own run/content stamp, including across content replacement.
		std::optional<ViewControlFrameObservation> m_PreviousFrame;
	};

	// Pure observation: no admission, lazy parameter tracking or revision writes.
	[[nodiscard]] ViewControlSnapshot BuildViewControlSnapshot(
		const ApplicationViewSettingsResolution& inherited,
		std::span<const LabParameterValue> overrides,
		const FrameCaptureFrameState& planned, const FrameCaptureFrameState* previous);
}
