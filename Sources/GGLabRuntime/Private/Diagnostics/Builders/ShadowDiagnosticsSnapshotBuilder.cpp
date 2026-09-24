#include "Diagnostics/Builders/ShadowDiagnosticsSnapshotBuilder.h"

#include "GGLabRuntime/Diagnostics/Snapshots/ShadowDiagnosticsSnapshot.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/DirectionalShadowFramePlan.h"
#include "GGLabRuntime/Graphics/RenderPass/ShadowGraphResources.h"

namespace gglab
{
	namespace
	{
		ShadowTextureDiagnostics BuildTextureDiagnostics(
			const RenderGraph& renderGraph, RGTextureId texture) noexcept
		{
			ShadowTextureDiagnostics diagnostics{};
			if (!texture.IsValid())
			{
				return diagnostics;
			}

			const RHITextureDesc& desc = renderGraph.GetTextureDesc(texture);
			diagnostics.m_Extent = desc.m_Extent;
			diagnostics.m_Format = desc.m_Format;
			diagnostics.m_ArraySize = desc.m_ArraySize;
			diagnostics.m_Available = true;
			return diagnostics;
		}
	}

	ShadowDiagnosticsSnapshot BuildShadowDiagnosticsSnapshot(
		const RenderGraph& renderGraph, const DirectionalShadowFramePlan* cascades,
		const RenderView* mainView, uint64_t frameSerial) noexcept
	{
		ShadowDiagnosticsSnapshot snapshot{};
		snapshot.m_FrameSerial = frameSerial;
		if (mainView) snapshot.m_MainView = *mainView;
		if (cascades)
		{
			snapshot.m_Settings = cascades->m_Settings;
			snapshot.m_LightDirection = cascades->m_LightDirection;
			snapshot.m_DistanceFadeStart = cascades->m_DistanceFadeStart;
			snapshot.m_Cascades.reserve(cascades->m_Cascades.size());
			for (uint32_t index = 0; index < cascades->m_Cascades.size(); ++index)
			{
				const auto& cascade = cascades->m_Cascades[index];
				const auto& queue = cascade.m_RenderQueue;
				const RenderQueueStatistics& statistics = queue.m_Statistics;
				snapshot.m_Cascades.push_back({
					.m_View = cascade.m_View,
					.m_QueueStatistics = {
						.m_TotalInstanceCount = statistics.m_TotalInstanceCount,
						.m_VisibleInstanceCount = statistics.m_VisibleInstanceCount,
						.m_CulledInstanceCount = statistics.m_CulledInstanceCount,
						.m_InvalidInstanceCount = statistics.m_InvalidInstanceCount,
						.m_UnboundedInstanceCount = statistics.m_UnboundedInstanceCount,
						.m_DrawItemCount = statistics.m_DrawItemCount,
					},
					.m_ViewIndex = cascades->GetViewIndex(index),
					.m_ShadowDrawCount =
						queue.m_BucketDrawRanges[utils::ToIndex(RenderBucket::Opaque)].m_Count +
						queue.m_BucketDrawRanges[utils::ToIndex(RenderBucket::AlphaTest)].m_Count,
					.m_SplitNear = cascade.m_SplitNear,
					.m_SplitFar = cascade.m_SplitFar,
					.m_BlendStart = cascade.m_BlendStart,
					.m_Projection = cascade.m_Projection,
					.m_Bias = cascade.m_Bias,
				});
			}
		}
		const auto* resources =
			renderGraph.GetBlackboard().TryGet<RGShadowResources>(ShadowResourcesName);
		if (!resources)
		{
			return snapshot;
		}

		snapshot.m_Available = true;
		snapshot.m_DirectionalShadowMap =
			BuildTextureDiagnostics(renderGraph, resources->m_DirectionalShadowMap);
		snapshot.m_DirectionalShadowMapPreviewSource = BuildTextureDiagnostics(
			renderGraph, resources->m_DirectionalShadowMapPreview);
		snapshot.m_ShadowMapSize = resources->m_ShadowMapSize;
		snapshot.m_ShadowMapPreviewSize = resources->m_ShadowMapPreviewSize;
		return snapshot;
	}
}
