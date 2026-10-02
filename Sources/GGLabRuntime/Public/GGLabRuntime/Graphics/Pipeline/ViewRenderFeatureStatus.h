#pragma once

#include <cstdint>

namespace gglab
{
	// Activity selected for the current frame; this is not GPU completion evidence.
	enum class ViewRenderFeatureState : uint8_t
	{
		Unavailable,
		Disabled,
		Active,
		Fallback,
		Inactive,
	};

	enum class ViewRenderFeatureReason : uint8_t
	{
		None,
		NotRequested,
		FrameUnavailable,
		PipelineUnavailable,
		CoreCapabilityUnavailable,
		GlobalLightCapacityExceeded,
		DepthCoverageUnavailable,
		RenderSceneUnavailable,
		NoOpaqueDraws,
		DisplayViewIneligible,
		DepthVelocityPathUnavailable,
		SceneExtensionUnsupported,
		MaterialDiagnosticsActive,
		AtmosphereUnavailable,
		PhysicalSunUnavailable,
		EnvironmentUnavailable,
		PhysicalSkyInactive,
		RequiredFeatureInactive,
		ZeroIntensity,
		ResourcesUnavailable,
		RenderGraphCulled,
	};

	struct ViewRenderFeatureStatus
	{
		ViewRenderFeatureState m_State = ViewRenderFeatureState::Unavailable;
		ViewRenderFeatureReason m_Reason = ViewRenderFeatureReason::FrameUnavailable;

		bool operator==(const ViewRenderFeatureStatus&) const noexcept = default;
	};
}
