#include "Diagnostics/Builders/AtmosphereDiagnosticsSnapshotBuilder.h"
#include "GGLabRuntime/Diagnostics/Snapshots/AtmosphereDiagnosticsSnapshot.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/RenderPass/AtmosphereGraphResources.h"
#include "Graphics/Renderer.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"

namespace gglab
{
	AtmosphereDiagnosticsSnapshot BuildAtmosphereDiagnosticsSnapshot(
		const Renderer& renderer, const RenderGraph& renderGraph) noexcept
	{
		AtmosphereDiagnosticsSnapshot snapshot{};
		if (const auto* resources =
			renderGraph.GetBlackboard().TryGet<RGAtmosphereResources>(AtmosphereResourcesName))
		{
			snapshot.m_State = resources->m_Diagnostics;
		}
		if (const auto* profiler = renderer.GetGpuProfiler())
		{
			const auto frame = profiler->GetLatestFrame();
			snapshot.m_GpuTimingAvailable = frame.IsValid();
			snapshot.m_GpuFrameIndex = frame.m_FrameIndex;
			for (const auto& sample : frame.m_Samples)
			{
				if (sample.m_Name.starts_with("Atmosphere."))
				{
					snapshot.m_GpuPasses.push_back({ sample.m_Name, sample.m_Milliseconds });
				}
			}
		}
		return snapshot;
	}
}
