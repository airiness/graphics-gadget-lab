#include "Diagnostics/Builders/TemporalAADiagnosticsSnapshotBuilder.h"

#include "Diagnostics/Snapshots/TemporalAADiagnosticsSnapshot.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/TemporalAAGraphResources.h"
#include "Graphics/RenderPass/TemporalGeometryGraphResources.h"
#include "Graphics/RenderView.h"

namespace gglab
{
	TemporalAADiagnosticsSnapshot BuildTemporalAADiagnosticsSnapshot(
		const Renderer& renderer, const RenderGraph& renderGraph,
		const ResolvedTemporalFramePlan* framePlan, const RenderView* displayView,
		const TemporalAASettings* authoringSettings,
		const TemporalAASettings* requestedSettings) noexcept
	{
		TemporalAADiagnosticsSnapshot snapshot{};
		snapshot.m_Available = framePlan != nullptr;
		if (framePlan)
		{
			snapshot.m_FramePlan = *framePlan;
		}
		if (authoringSettings)
		{
			snapshot.m_AuthoringSettings = *authoringSettings;
		}
		if (requestedSettings)
		{
			snapshot.m_RequestedSettings = *requestedSettings;
		}
		if (displayView)
		{
			snapshot.m_CurrentJitterPixels = displayView->m_JitterPixels;
			snapshot.m_CurrentJitterUV = displayView->m_JitterUV;
			snapshot.m_PreviousJitterUV = displayView->m_PreviousJitterUV;
			snapshot.m_Width = displayView->m_Width;
			snapshot.m_Height = displayView->m_Height;
		}

		if (const auto* historyManager = renderer.GetTemporalHistoryManager())
		{
			snapshot.m_History = historyManager->GetDiagnostics();
		}
		const auto* temporalResources = renderGraph.GetBlackboard().TryGet<
			RGTemporalAAResources>(TemporalAAResourcesName);
		if (temporalResources)
		{
			snapshot.m_TemporalResourcesAvailable = temporalResources->IsValid();
			snapshot.m_ReprojectionDiagnosticsAvailable =
				temporalResources->m_ReprojectionDiagnostics.IsValid();
			snapshot.m_Width = temporalResources->m_Width;
			snapshot.m_Height = temporalResources->m_Height;
			if (temporalResources->m_ResolvedSceneColor.IsValid())
			{
				snapshot.m_ResolvedColorFormat = renderGraph.GetTextureDesc(
					temporalResources->m_ResolvedSceneColor).m_Format;
			}
		}
		const auto* temporalGeometry = renderGraph.GetBlackboard().TryGet<
			RGTemporalGeometryResources>(TemporalGeometryResourcesName);
		snapshot.m_MotionAvailable = temporalGeometry && temporalGeometry->IsValid();

		const GpuProfiler* gpuProfiler = renderer.GetGpuProfiler();
		snapshot.m_GpuProfilerEnabled = gpuProfiler && gpuProfiler->IsEnabled();
		if (gpuProfiler)
		{
			const GpuProfileFrameSnapshot gpuFrame = gpuProfiler->GetLatestFrame();
			snapshot.m_GpuFrameIndex = gpuFrame.m_FrameIndex;
			for (const auto& sample : gpuFrame.m_Samples)
			{
				if (sample.m_Name == "PostProcess.TemporalAA")
				{
					snapshot.m_GpuTimingAvailable = true;
					snapshot.m_ResolveGpuMilliseconds += sample.m_Milliseconds;
				}
			}
		}
		return snapshot;
	}
}
