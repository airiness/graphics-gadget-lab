#pragma once

#include "GGLabRuntime/Graphics/EnvironmentLightingSettings.h"

#include <cstdint>
#include <optional>

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

	// Copied from the inputs and resources considered by the aerial pass,
	// including frames where aerial transport itself was not requested.
	struct AerialPerspectiveDependencies
	{
		bool m_SceneAvailable = false;
		bool m_AtmosphereEnabled = false;
		bool m_AtmosphereReady = false;
		bool m_PhysicalSunEnabled = false;
		std::optional<EnvironmentBackgroundMode> m_SkySource;
		bool m_SkyboxEnabled = false;
	};
}
