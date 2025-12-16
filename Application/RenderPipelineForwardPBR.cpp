#include "Precompiled.h"
#include "RenderPipelineForwardPBR.h"
#include "Renderer.h"
#include "DX12Device.h"
#include "DX12SwapChain.h"
#include "RGFrameTargets.h"

namespace gglab
{
	void RenderPipelineForwardPBR::BuildRenderGraph(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(context.IsValid(), "RenderFrameContext invalid.");
		GGLAB_ASSERT_MSG(services.IsValid(), "RenderServices invalid.");

		auto* renderer = services.m_Renderer;
		auto* swapchain = renderer->GetDevice()->GetSwapChain();

		struct SetupData {};
		rg.AddPass<SetupData>("MainView.Setup",
			[swapchain](RenderGraph::RGBuilder& builder, SetupData&)
			{
				auto& blackboard = builder.GetBlackboard();
				auto& targets = blackboard.GetOrCreate<RGFrameTargets>(MainViewName);

				RGTextureDesc backBufferDesc{};
				backBufferDesc.m_Width = swapchain->GetBufferWidth();
				backBufferDesc.m_Height = swapchain->GetBufferHeight();
				backBufferDesc.m_Format = swapchain->GetFormat();
				backBufferDesc.m_Usage = RGTextureUsage::RenderTarget;

				targets.m_BackBuffer = builder.Import("MainView.BackBuffer",
					swapchain->GetCurrentBackBuffer(),
					backBufferDesc,
					D3D12_RESOURCE_STATE_PRESENT);
			});


		if (m_Settings.m_EnableForwardPBRPass)
		{
			m_ForwardPBRPass.AddPass(rg, context, services);
		}
	}
}