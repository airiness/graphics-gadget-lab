#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalHistoryTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/ViewRenderFeatureStatus.h"
#include "GGLabRuntime/Graphics/PostProcess/ViewRenderSettings.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"

#include <cstdint>
#include <optional>

namespace gglab
{
	// Owned CPU values for one display view and frame. Active describes the
	// compiled graph's selected work, never a completed GPU sample or preview.
	struct RenderingSettingsDiagnosticsSnapshot
	{
		ViewRenderProfile m_AuthoringProfile{};
		ViewRenderProfile m_RequestedProfile{};
		ResolvedViewRenderSettings m_ResolvedSettings{};
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		uint64_t m_FrameSerial = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		bool m_SettingsAvailable = false;
		bool m_RuntimeAvailable = false;

		ViewRenderFeatureStatus m_ForwardLighting{};
		std::optional<ForwardLightingMode> m_ActualLightingMode;
		ViewRenderFeatureStatus m_GTAO{};
		bool m_GTAOUsesFormatFallback = false;
		ViewRenderFeatureStatus m_TemporalAA{};
		TemporalHistorySummary m_History{};
		bool m_HistoryAvailable = false;
		ViewRenderFeatureStatus m_Bloom{};
		ViewRenderFeatureStatus m_ScenePreExposure{};
		ViewRenderFeatureStatus m_HdrDiffValidation{};
		ViewRenderFeatureStatus m_ToneMapping{};
		ViewRenderFeatureStatus m_Shadows{};
		DirectionalShadowSettings m_ShadowSettings = DisabledDirectionalShadowSettings();
	};

	template <> struct SnapshotTraits<RenderingSettingsDiagnosticsSnapshot>
	{
		static constexpr SnapshotId Id =
			MakeSnapshotId("Diagnostics.RenderingSettingsDiagnosticsSnapshot");
	};
}
