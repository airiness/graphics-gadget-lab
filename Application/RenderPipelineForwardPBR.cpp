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
		auto* swapChain = renderer->GetDevice()->GetSwapChain();

		// MainView Setup
		struct SetupData {};
		rg.AddPass<SetupData>("MainView.Setup",
			[swapChain, &rg](RenderGraph::RGBuilder& builder, SetupData&)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& targets = blackboard.GetOrCreate<RGFrameTargets>(MainViewName);

				const uint32_t width = swapChain->GetBufferWidth();
				const uint32_t height = swapChain->GetBufferHeight();

				// Import backbuffer
				RGTextureDesc backBufferDesc{};
				backBufferDesc.m_Width = width;
				backBufferDesc.m_Height = height;
				backBufferDesc.m_Format = swapChain->GetFormat();
				backBufferDesc.m_Usage = RGTextureUsage::RenderTarget;

				targets.m_BackBuffer = builder.ImportTexture("MainView.BackBuffer",
					swapChain->GetCurrentBackBuffer(),
					backBufferDesc,
					D3D12_RESOURCE_STATE_PRESENT);

				// Create depth buffer
				RGTextureDesc depthBufferDesc{};
				depthBufferDesc.m_Width = width;
				depthBufferDesc.m_Height = height;
				depthBufferDesc.m_Format = DXGI_FORMAT_D32_FLOAT;
				depthBufferDesc.m_Usage = RGTextureUsage::DepthStencil;

				targets.m_Depth = builder.CreateTexture("MainView.DepthBuffer",
					depthBufferDesc);

				targets.m_Width = width;
				targets.m_Height = height;

				// RTVview Key
				const auto resourceIndex = rg.GetResourceIndex(targets.m_BackBuffer);
				targets.m_BackBufferRTVKey = DX12ViewCache::BuildKey<ViewType::RTV>(
					resourceIndex,
					swapChain->GetCurrentBackBuffer());
			});

		// SwapChain prepare backbuffer
		struct PrepareBackBufferData
		{
			RGTextureId m_BackBuffer{};
		};

		rg.AddPass<PrepareBackBufferData>("SwapChain.PrepareBackBuffer",
			[](RenderGraph::RGBuilder& builder, PrepareBackBufferData& data)
			{
				auto& targets = builder.GetBlackboard().Get<RGFrameTargets>(MainViewName);

				data.m_BackBuffer = builder.Write(targets.m_BackBuffer,
					RGTextureUsage::RenderTarget);
			},
			[swapChain](DX12CommandList* commandList, PrepareBackBufferData&) 
			{
				swapChain->PrepareBackBuffer(commandList);
				commandList->FlushBarriers();
			});

		// Clear backbuffer
		struct ClearBackBufferData
		{
			RGTextureId m_BackBuffer{};
			ViewKey m_RTVKey{};
		};
		rg.AddPass<ClearBackBufferData>("SwapChain.ClearBackBuffer",
			[](RenderGraph::RGBuilder& builder, ClearBackBufferData& data)
			{
				auto& targets = builder.GetBlackboard().Get<RGFrameTargets>(MainViewName);

				data.m_BackBuffer = builder.Write(targets.m_BackBuffer,
					RGTextureUsage::RenderTarget);
				data.m_RTVKey = targets.m_BackBufferRTVKey;
			},
			[&rg, swapChain](DX12CommandList* commandList, ClearBackBufferData& data)
			{
				auto* backTexture = rg.GetTexture(data.m_BackBuffer);

				GGLAB_ASSERT(backTexture);

				auto* viewCache = rg.GetViewCache();
				const auto& rtv = viewCache->GetOrCreate(data.m_RTVKey, backTexture);

				commandList->ClearRenderTarget(rtv, swapChain->GetClearColor());		
			});

		// RenderPass ForwardPBR
		if (m_Settings.m_EnableForwardPBRPass)
		{
			m_ForwardPBRPass.AddPass(rg, context, services);
		}

		// Finish backbuffer
		struct FinishBackBufferData
		{
			RGTextureId m_BackBuffer{};
		};
		rg.AddPass<FinishBackBufferData>("SwapChain.FinishBackBuffer",
			[](RenderGraph::RGBuilder& builder, FinishBackBufferData& data)
			{
				auto& targets = builder.GetBlackboard().Get<RGFrameTargets>(MainViewName);

				data.m_BackBuffer = builder.Write(targets.m_BackBuffer,
					RGTextureUsage::RenderTarget);

			},
			[swapChain](DX12CommandList* commandList, FinishBackBufferData& data)
			{
				swapChain->FinishBackBuffer(commandList);
				commandList->FlushBarriers();
			});
	}
}