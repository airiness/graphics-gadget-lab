#include "Precompiled.h"
#include "RenderPassDevelopGui.h"
#include "Renderer.h"
#include "RenderGraph.h"
#include "RGFrameTargets.h"
#include "RGIBLResources.h"
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
			RGTextureId m_BrdfLut{};
			ViewKey m_RtvKey{};
		};

		rg.AddPass<DevelopGuiData>("RenderPassDevelopGui",
			[](RenderGraph::RGBuilder& builder, DevelopGuiData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();

				auto& targetsTable = blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& viewTargets = targetsTable.GetViewTargets(RenderViewID::Main);
				data.m_BackBuffer = builder.Write(viewTargets.m_Color, RGTextureUsage::RenderTarget);
				data.m_RtvKey = viewTargets.m_BackBufferRTVKey;

				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);
				data.m_BrdfLut = builder.Read(iblRes.m_BrdfLut, RGTextureUsage::Sample);
			},
			[&rg, &services](RGExecuteContext& executeContext, DevelopGuiData& data)
			{
				auto* developGui = services.m_Renderer->GetDevelopGui();

				auto* backTexture = rg.GetTexture(data.m_BackBuffer);
				auto* viewCache = rg.GetViewCache();
				const auto& rtv = viewCache->GetOrCreate(data.m_RtvKey, backTexture);
				developGui->Render(executeContext.m_GraphicsCommandList, rtv);
			});
	}
}