#include "Precompiled.h"
#include "RenderPassDevelopGui.h"
#include "Renderer.h"
#include "RenderGraph.h"
#include "RGFrameTargets.h"
#include "DevelopGui.h"

namespace gglab
{
	void RenderPassDevelopGui::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		struct DevelopGuiData
		{
			RGTextureId m_BackBuffer{};
			ViewKey m_RTVKey{};
		};

		rg.AddPass<DevelopGuiData>("RenderPassDevelopGui",
			[](RenderGraph::RGBuilder& builder, DevelopGuiData& data)
			{
				builder.SideEffect();

				auto& targetsTable = builder.GetBlackboard().GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& viewTargets = targetsTable.GetViewTargets(RenderViewID::Main);
				data.m_BackBuffer = builder.Write(viewTargets.m_Color, RGTextureUsage::RenderTarget);
				data.m_RTVKey = viewTargets.m_BackBufferRTVKey;
			},
			[&rg, &services](DX12CommandList* commandList, DevelopGuiData& data)
			{
				auto* developGui = services.m_Renderer->GetDevelopGui();

				auto* backTexture = rg.GetTexture(data.m_BackBuffer);
				auto* viewCache = rg.GetViewCache();
				const auto& rtv = viewCache->GetOrCreate(data.m_RTVKey, backTexture);
				developGui->Render(commandList, rtv);
			});
	}
}