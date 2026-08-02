#pragma once

#include "Diagnostics/SnapshotCommon.h"
#include "Graphics/Pipeline/GTAO.h"
#include "Graphics/PostProcess/ViewRenderSettings.h"
#include "Graphics/RHI/RHITypes.h"

namespace gglab
{
	struct GTAOTextureDiagnostics
	{
		uint64_t m_LogicalBytes = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_Format = RHIFormat::Unknown;
		bool m_Available = false;
	};

	struct GTAODiagnosticsSnapshot
	{
		GTAOSettings m_AuthoringSettings{};
		GTAOSettings m_RequestedSettings{};
		GTAOSettings m_ResolvedSettings{};
		GTAOCapabilityStatus m_Capabilities{};
		GTAOFrameStatus m_Status = GTAOFrameStatus::Disabled;

		GTAOTextureDiagnostics m_RawAO{};
		GTAOTextureDiagnostics m_HalfDepthViewZ{};
		GTAOTextureDiagnostics m_DenoiseX{};
		GTAOTextureDiagnostics m_DenoiseY{};
		GTAOTextureDiagnostics m_FinalAO{};
		GTAOTextureDiagnostics m_ReconstructedNormal{};
		GTAOTextureDiagnostics m_SelectedSurfaceOffset{};
		GTAOTextureDiagnostics m_AOOnlyLightingContribution{};

		uint64_t m_CoreLogicalBytes = 0;
		uint64_t m_DiagnosticLogicalBytes = 0;
		uint64_t m_TotalLogicalBytes = 0;
		uint64_t m_GpuFrameIndex = 0;
		double m_EvaluateGpuMilliseconds = 0.0;
		double m_DenoiseXGpuMilliseconds = 0.0;
		double m_DenoiseYGpuMilliseconds = 0.0;
		double m_UpsampleGpuMilliseconds = 0.0;
		double m_TotalGpuMilliseconds = 0.0;

		bool m_Available = false;
		bool m_OverrideActive = false;
		bool m_CoreResourcesAllocated = false;
		bool m_DiagnosticResourcesAllocated = false;
		bool m_UsesFinalAOFormatFallback = false;
		bool m_GpuProfilerEnabled = false;
		bool m_GpuTimingAvailable = false;
	};

	template <> struct SnapshotTraits<GTAODiagnosticsSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.GTAODiagnosticsSnapshot");
	};
}
