#include "Diagnostics/Builders/AtmosphereDiagnosticsSnapshotBuilder.h"
#include "GGLabRuntime/Diagnostics/Snapshots/AtmosphereDiagnosticsSnapshot.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/RenderPass/AtmosphereGraphResources.h"
#include "Graphics/Renderer.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/Resource/PersistentTexturePool.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"

namespace gglab
{
	AtmosphereDiagnosticsSnapshot BuildAtmosphereDiagnosticsSnapshot(
		const Renderer& renderer, const RenderGraph& renderGraph) noexcept
	{
		AtmosphereDiagnosticsSnapshot snapshot{};
		const auto* environment = renderer.GetEnvironmentLightingSystemService();
		if (environment)
		{
			snapshot.m_PhysicalSkyRequested = environment->GetRequestedPhysicalSky().has_value();
			snapshot.m_PhysicalSkyActive = environment->GetActivePhysicalSky().has_value();
			if (const auto& active = environment->GetActivePhysicalSky()) snapshot.m_ActiveSun = active->m_Sun;
			snapshot.m_PublicationMilliseconds = environment->GetPublicationMilliseconds();
			snapshot.m_RequestedWorldLightingGeneration = environment->GetBakeRequestGeneration();
		}
		if (const auto* bake = renderer.GetIBLBakeScheduler())
		{
			snapshot.m_ActiveWorldLightingGeneration = bake->GetStatus().m_ActiveGeneration;
		}
		if (const auto* pool = renderer.GetPersistentTexturePool())
		{
			snapshot.m_RetiringTextureCount = pool->GetDiagnostics().m_PendingRetirementTextureCount;
		}
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
				if (sample.m_Name.starts_with("Atmosphere.") || sample.m_Name.starts_with("IBL.Atmosphere."))
				{
					snapshot.m_GpuPasses.push_back({ sample.m_Name, sample.m_Milliseconds });
				}
			}
		}
		return snapshot;
	}
}
