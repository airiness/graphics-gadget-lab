#include "Graphics/RenderPass/RenderPassClearViewTargets.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"

#include <array>
#include <span>

namespace gglab
{
	namespace
	{
		struct PassData
		{
			RGTextureId m_SceneColor{};
			RGTextureViewId m_Rtv{};
			std::array<RGTextureViewId, 2> m_DiagnosticRtvs{};
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
				if (targets.m_MaterialDiagnosticColor.IsValid())
				{
					const std::array diagnostics{ &targets.m_MaterialDiagnosticColor,
						&targets.m_MaterialDiagnosticCoverage };
					for (size_t index = 0; index < diagnostics.size(); ++index)
					{
						builder.WriteInPlace(*diagnostics[index], RGTextureAccess::RenderTarget);
						data.m_DiagnosticRtvs[index] =
							builder.CreateView<RHITextureViewType::RenderTarget>(*diagnostics[index]);
					}
				}
			},
			[](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				std::array<RHIRenderingAttachment, 3> attachments{};
				attachments[0] = RHIRenderingAttachment{
					.m_View = rtv,
					.m_LoadOp = RHIContentLoadOp::DontCare,
				};
				uint32_t targetCount = 1;
				if (data.m_DiagnosticRtvs[0].IsValid())
				{
					for (const auto view : data.m_DiagnosticRtvs)
					{
						attachments[targetCount++] = { .m_View = executeContext.GetViewHandle(view),
							.m_LoadOp = RHIContentLoadOp::DontCare };
					}
				}
				commandContext->BeginRendering({ .m_ColorAttachments =
					std::span<const RHIRenderingAttachment>(attachments.data(), targetCount) });
				commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
				for (uint32_t index = 1; index < targetCount; ++index)
				{
					commandContext->ClearColorAttachment(index, {});
				}
			});
	}
}
