#include "Graphics/RenderPass/RenderPassClearViewTargets.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"

#include <span>

namespace gglab
{
	namespace
	{
		struct PassData
		{
			RGTextureId m_SceneColor{};
			RGTextureViewId m_Rtv{};
		};
	}

	void RenderPassClearViewTargets::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_UNUSED(services);
		const RenderViewID displayViewId = context.GetDisplayViewId();

		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[displayViewId](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& targets = builder.GetBlackboard()
					.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(displayViewId);

				builder.WriteInPlace(targets.m_SceneColor, RGTextureAccess::RenderTarget);
				data.m_SceneColor = targets.m_SceneColor;
				data.m_Rtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_SceneColor);
			},
			[](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				const RHIRenderingAttachment colorAttachment{
					.m_View = rtv,
					.m_LoadOp = RHIContentLoadOp::DontCare,
				};
				commandContext->BeginRendering({ .m_ColorAttachments =
					std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
				commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
			});
	}
}
