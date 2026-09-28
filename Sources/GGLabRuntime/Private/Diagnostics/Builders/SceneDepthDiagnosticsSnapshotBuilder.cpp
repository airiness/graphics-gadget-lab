#include "Diagnostics/Builders/SceneDepthDiagnosticsSnapshotBuilder.h"
#include "GGLabRuntime/Diagnostics/Snapshots/SceneDepthDiagnosticsSnapshot.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"

namespace gglab
{
	SceneDepthDiagnosticsSnapshot BuildSceneDepthDiagnosticsSnapshot(
		const RenderGraph& renderGraph) noexcept
	{
		SceneDepthDiagnosticsSnapshot snapshot{};
		const auto* resources =
			renderGraph.GetBlackboard().TryGet<RGSceneDepthResources>(SceneDepthResourcesName);
		if (!resources || !resources->m_Texture.IsValid()) return snapshot;
		const auto& desc = renderGraph.GetTextureDesc(resources->m_Texture);
		snapshot = {
			.m_Width = static_cast<uint32_t>(desc.m_Extent.m_Width),
			.m_Height = desc.m_Extent.m_Height,
			.m_ResourceFormat = desc.m_Format,
			.m_DsvFormat = resources->m_DsvDesc.m_Format,
			.m_SrvFormat = resources->m_SrvDesc.m_Format,
			.m_ClearDepth = desc.m_ClearValue ? desc.m_ClearValue->m_Depth : 0.0f,
			.m_Convention = resources->m_Convention,
			.m_HasTypedClear = desc.m_ClearValue.has_value(),
			.m_Available = true,
		};
		return snapshot;
	}
}
