#pragma once

#include "GGLabRuntime/Core/Math/Vector.h"
#include "Diagnostics/SnapshotCommon.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "Graphics/Pipeline/TemporalHistoryManager.h"

namespace gglab
{
	struct TemporalAADiagnosticsSnapshot
	{
		ResolvedTemporalFramePlan m_FramePlan{};
		TemporalHistoryManagerDiagnostics m_History{};
		TemporalAASettings m_AuthoringSettings{};
		TemporalAASettings m_RequestedSettings{};
		Vector2 m_CurrentJitterPixels = Vector2::Zero;
		Vector2 m_CurrentJitterUV = Vector2::Zero;
		Vector2 m_PreviousJitterUV = Vector2::Zero;
		RHIFormat m_ResolvedColorFormat = RHIFormat::Unknown;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint64_t m_GpuFrameIndex = 0;
		double m_ResolveGpuMilliseconds = 0.0;
		bool m_Available = false;
		bool m_TemporalResourcesAvailable = false;
		bool m_ReprojectionDiagnosticsAvailable = false;
		bool m_MotionAvailable = false;
		bool m_GpuProfilerEnabled = false;
		bool m_GpuTimingAvailable = false;
	};

	template <> struct SnapshotTraits<TemporalAADiagnosticsSnapshot>
	{
		static constexpr SnapshotId Id =
			MakeSnapshotId("Diagnostics.TemporalAADiagnosticsSnapshot");
	};
}
