#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderQueueSnapshot.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RenderView.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"

#include <cstdint>
#include <vector>

namespace gglab
{
	struct ShadowTextureDiagnostics
	{
		RHIExtent3D m_Extent{};
		RHIFormat m_Format = RHIFormat::Unknown;
		uint32_t m_ArraySize = 0;
		bool m_Available = false;
	};

	struct DirectionalShadowCascadeSnapshot
	{
		RenderView m_View{};
		RenderQueueStatisticsSnapshot m_QueueStatistics{};
		uint32_t m_ViewIndex = 0;
		// Only opaque and alpha-test buckets are submitted to the shadow pass.
		uint32_t m_ShadowDrawCount = 0;
		float m_SplitNear = 0.0f;
		float m_SplitFar = 0.0f;
		float m_BlendStart = 0.0f;
		DirectionalShadowProjectionInfo m_Projection{};
		DirectionalShadowResolvedBias m_Bias{};
	};

	struct ShadowDiagnosticsSnapshot
	{
		std::vector<DirectionalShadowCascadeSnapshot> m_Cascades;
		RenderView m_MainView{};
		DirectionalShadowSettings m_Settings = DisabledDirectionalShadowSettings();
		Vector3 m_LightDirection = Vector3::Zero;
		uint64_t m_FrameSerial = 0;
		float m_DistanceFadeStart = 0.0f;
		ShadowTextureDiagnostics m_DirectionalShadowMap{};
		ShadowTextureDiagnostics m_DirectionalShadowMapPreviewSource{};
		uint32_t m_ShadowMapSize = 0;
		uint32_t m_ShadowMapPreviewSize = 0;
		bool m_Available = false;
	};

	template <> struct SnapshotTraits<ShadowDiagnosticsSnapshot>
	{
		static constexpr SnapshotId Id =
			MakeSnapshotId("Diagnostics.ShadowDiagnosticsSnapshot");
	};
}
