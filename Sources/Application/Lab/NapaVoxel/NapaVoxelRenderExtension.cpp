#include "Core/Precompiled.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRenderExtension.h"

namespace gglab
{
	void NapaVoxelRenderExtension::AddOpaqueScenePasses(RenderGraph& renderGraph,
		const RenderFrameContext& frameContext, const RenderServices& services) noexcept
	{
		m_RenderPass.SetFrameView(m_FrameSource ? m_FrameSource->GetFrameView() : nullptr);
		m_RenderPass.AddPass(renderGraph, frameContext, services);
	}
}
