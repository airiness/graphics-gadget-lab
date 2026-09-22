#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderQueueSnapshot.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RenderView.h"

#include <cstdint>
#include <vector>

namespace gglab
{
	struct ShadowTextureDiagnostics
	{
		RHIExtent3D m_Extent{};
		RHIFormat m_Format = RHIFormat::Unknown;
		bool m_Available = false;
	};

	struct DirectionalShadowCascadeSnapshot
	{
		RenderView m_View{};
		RenderQueueStatisticsSnapshot m_QueueStatistics{};
		uint32_t m_ViewIndex = 0;
		// Only opaque and alpha-test buckets are submitted to the shadow pass.
		uint32_t m_ShadowDrawCount = 0;
	};

	struct ShadowDiagnosticsSnapshot
	{
		std::vector<DirectionalShadowCascadeSnapshot> m_Cascades;
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
