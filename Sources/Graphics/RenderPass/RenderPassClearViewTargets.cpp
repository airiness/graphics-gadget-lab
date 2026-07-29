#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassClearViewTargets.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RHI/RHITextureViewDescUtils.h"

namespace gglab
{
	namespace
	{
		struct PassData
		{
			RGTextureId m_SceneColor{};
			RGTextureId m_Depth{};
			RGTextureViewId m_Rtv{};
			RGTextureViewId m_Dsv{};
		};
	}

	void RenderPassClearViewTargets::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_UNUSED(services);
		const RenderViewID displayViewId = context.GetDisplayViewId();

		rg.AddPass<PassData>(GetRenderGraphPassName(),
			[displayViewId](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& targets = builder.GetBlackboard()
					.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(displayViewId);

				builder.WriteInPlace(targets.m_SceneColor, RGTextureAccess::RenderTarget);
				builder.WriteInPlace(targets.m_Depth, RGTextureAccess::DepthStencilWrite);
				data.m_SceneColor = targets.m_SceneColor;
				data.m_Depth = targets.m_Depth;
				data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(data.m_SceneColor);
				RHITextureViewDesc dsvDesc =
					MakeRHITexture2DViewDesc(
						RHIFormat::D32Float,
						0,
						1,
						RHITextureAspect::Depth);
				data.m_Dsv = builder.CreateView<RHITextureViewType::DepthStencil>(
					data.m_Depth,
					dsvDesc);
			},
			[](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				const auto dsv = executeContext.GetViewHandle(data.m_Dsv);
				commandContext->ClearColor(rtv, { 0.0f, 0.0f, 0.0f, 1.0f });
				commandContext->ClearDepthStencil(dsv, 0.0f);
			});
	}
}
