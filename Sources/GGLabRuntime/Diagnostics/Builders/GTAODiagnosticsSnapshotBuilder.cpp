#include "Diagnostics/Builders/GTAODiagnosticsSnapshotBuilder.h"

#include "Diagnostics/Snapshots/GTAODiagnosticsSnapshot.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPass/GTAOGraphResources.h"
#include "Graphics/RHI/RHIFormat.h"

namespace gglab
{
	namespace
	{
		GTAOTextureDiagnostics BuildTextureDiagnostics(
			const RenderGraph& renderGraph, RGTextureId texture) noexcept
		{
			GTAOTextureDiagnostics diagnostics{};
			if (!texture.IsValid())
			{
				return diagnostics;
			}
			const RHITextureDesc& desc = renderGraph.GetTextureDesc(texture);
			diagnostics.m_Width = static_cast<uint32_t>(desc.m_Extent.m_Width);
			diagnostics.m_Height = desc.m_Extent.m_Height;
			diagnostics.m_Format = desc.m_Format;
			diagnostics.m_LogicalBytes = desc.m_Extent.m_Width * desc.m_Extent.m_Height *
				desc.m_Extent.m_Depth * desc.m_ArraySize *
				GetRHIFormatInfo(desc.m_Format).m_BytesPerBlock;
			diagnostics.m_Available = true;
			return diagnostics;
		}
	}

	GTAODiagnosticsSnapshot BuildGTAODiagnosticsSnapshot(
		const Renderer& renderer, const RenderGraph& renderGraph,
		const GTAOSettings* authoringSettings, const GTAOSettings* requestedSettings,
		bool overrideActive) noexcept
	{
		GTAODiagnosticsSnapshot snapshot{};
		if (authoringSettings)
		{
			snapshot.m_AuthoringSettings = *authoringSettings;
		}
		if (requestedSettings)
		{
			snapshot.m_RequestedSettings = *requestedSettings;
		}
		snapshot.m_OverrideActive = overrideActive;

		const auto* resources =
			renderGraph.GetBlackboard().TryGet<RGGTAOResources>(GTAOResourcesName);
		if (!resources)
		{
			return snapshot;
		}
		snapshot.m_Available = true;
		snapshot.m_Status = resources->m_Status;
		snapshot.m_Capabilities = resources->m_Capabilities;
		snapshot.m_ResolvedSettings = resources->m_ResolvedSettings;
		snapshot.m_RawAO = BuildTextureDiagnostics(renderGraph, resources->m_RawAO);
		snapshot.m_HalfDepthViewZ =
			BuildTextureDiagnostics(renderGraph, resources->m_HalfDepthViewZ);
		snapshot.m_DenoiseX = BuildTextureDiagnostics(renderGraph, resources->m_DenoiseX);
		snapshot.m_DenoiseY = BuildTextureDiagnostics(renderGraph, resources->m_DenoiseY);
		snapshot.m_FinalAO = BuildTextureDiagnostics(renderGraph, resources->m_FinalAO);
		snapshot.m_ReconstructedNormal =
			BuildTextureDiagnostics(renderGraph, resources->m_ReconstructedNormal);
		snapshot.m_SelectedSurfaceOffset =
			BuildTextureDiagnostics(renderGraph, resources->m_SelectedSurfaceOffset);
		snapshot.m_AOOnlyLightingContribution =
			BuildTextureDiagnostics(renderGraph, resources->m_AOOnlyLightingContribution);

		snapshot.m_CoreLogicalBytes = snapshot.m_RawAO.m_LogicalBytes +
			snapshot.m_HalfDepthViewZ.m_LogicalBytes + snapshot.m_DenoiseX.m_LogicalBytes +
			snapshot.m_DenoiseY.m_LogicalBytes + snapshot.m_FinalAO.m_LogicalBytes;
		snapshot.m_DiagnosticLogicalBytes = snapshot.m_ReconstructedNormal.m_LogicalBytes +
			snapshot.m_SelectedSurfaceOffset.m_LogicalBytes +
			snapshot.m_AOOnlyLightingContribution.m_LogicalBytes;
		snapshot.m_TotalLogicalBytes =
			snapshot.m_CoreLogicalBytes + snapshot.m_DiagnosticLogicalBytes;
		snapshot.m_CoreResourcesAllocated = resources->IsComplete();
		snapshot.m_DiagnosticResourcesAllocated =
			snapshot.m_ReconstructedNormal.m_Available ||
			snapshot.m_SelectedSurfaceOffset.m_Available ||
			snapshot.m_AOOnlyLightingContribution.m_Available;
		snapshot.m_UsesFinalAOFormatFallback =
			snapshot.m_RequestedSettings.m_FinalAOFormatPreference ==
			GTAOFinalAOFormatPreference::PreferR8Unorm &&
			resources->m_FinalAOFormat == RHIFormat::R16Float &&
			!resources->m_Capabilities.m_FinalAO.m_PreferredR8Unorm.IsSupported();

		const GpuProfiler* profiler = renderer.GetGpuProfiler();
		snapshot.m_GpuProfilerEnabled = profiler && profiler->IsEnabled();
		if (!profiler)
		{
			return snapshot;
		}
		const GpuProfileFrameSnapshot frame = profiler->GetLatestFrame();
		snapshot.m_GpuFrameIndex = frame.m_FrameIndex;
		bool foundGTAOSample = false;
		for (const GpuProfileSample& sample : frame.m_Samples)
		{
			if (sample.m_Name == "Lighting.GTAO")
			{
				foundGTAOSample = true;
				snapshot.m_EvaluateGpuMilliseconds += sample.m_Milliseconds;
			}
			else if (sample.m_Name == "Lighting.GTAO.DenoiseX")
			{
				foundGTAOSample = true;
				snapshot.m_DenoiseXGpuMilliseconds += sample.m_Milliseconds;
			}
			else if (sample.m_Name == "Lighting.GTAO.DenoiseY")
			{
				foundGTAOSample = true;
				snapshot.m_DenoiseYGpuMilliseconds += sample.m_Milliseconds;
			}
			else if (sample.m_Name == "Lighting.GTAO.Upsample")
			{
				foundGTAOSample = true;
				snapshot.m_UpsampleGpuMilliseconds += sample.m_Milliseconds;
			}
		}
		snapshot.m_GpuTimingAvailable = frame.IsValid() && foundGTAOSample;
		snapshot.m_TotalGpuMilliseconds = snapshot.m_EvaluateGpuMilliseconds +
			snapshot.m_DenoiseXGpuMilliseconds + snapshot.m_DenoiseYGpuMilliseconds +
			snapshot.m_UpsampleGpuMilliseconds;
		return snapshot;
	}
}
