#include "Precompiled.h"
#include "RenderPipelineForwardPBR.h"

namespace gglab
{
	void RenderPipelineForwardPBR::BuildRenderGraph(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(context.IsValid(), "RenderFrameContext invalid.");
		GGLAB_ASSERT_MSG(services.IsValid(), "RenderServices invalid.");

		if (m_Settings.m_EnableForwardPBRPass)
		{
			m_ForwardPBRPass.AddPass(rg, context, services);
		}
	}
}