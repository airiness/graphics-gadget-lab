#include "Diagnostics/Builders/ShadowDiagnosticsSnapshotBuilder.h"

#include "GGLabRuntime/Diagnostics/Snapshots/ShadowDiagnosticsSnapshot.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/ShadowGraphResources.h"

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
			diagnostics.m_Available = true;
			return diagnostics;
		}
	}

	ShadowDiagnosticsSnapshot BuildShadowDiagnosticsSnapshot(
		const RenderGraph& renderGraph) noexcept
	{
		ShadowDiagnosticsSnapshot snapshot{};
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
