#include "Core/Precompiled.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Graphics/Renderer.h"
#include "Graphics/DX12/DX12Device.h"
#include "Graphics/DX12/DX12SwapChain.h"
#include "Graphics/RenderGraph/RGFrameTargets.h"

namespace gglab
{
	void RenderPipelineForwardPBR::BuildRenderGraph(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(context.IsValid(), "RenderFrameContext invalid.");
		GGLAB_ASSERT_MSG(services.IsValid(), "RenderServices invalid.");

		auto* renderer = services.m_Renderer;
		auto* swapChain = renderer->GetSwapChain();

		const uint32_t frameBackBufferIndex = context.m_BackBufferIndex;

		// MainView Setup
		struct SetupData {};
		rg.AddPass<SetupData>("MainView.Setup",
			[swapChain, &rg, renderer, frameBackBufferIndex](RenderGraph::RGBuilder& builder, SetupData&)
			{
				//GGLAB_LOG_GRAPHICS_INFO("MainView.Setup(Setup)");
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& targetsTable = blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& targets = targetsTable.GetViewTargets(RenderViewID::Main);

				const uint32_t width = swapChain->GetBufferWidth();
				const uint32_t height = swapChain->GetBufferHeight();

				targets.m_Width = width;
				targets.m_Height = height;

				targets.m_BackBufferIndex = frameBackBufferIndex;
				auto* backTexture = swapChain->GetBackBuffer(frameBackBufferIndex);
				GGLAB_ASSERT(backTexture);

				// Import backbuffer
				RGTextureDesc backBufferDesc{};
				backBufferDesc.m_Width = width;
				backBufferDesc.m_Height = height;
				backBufferDesc.m_Format = swapChain->GetFormat();
				backBufferDesc.m_Usage = RGTextureUsage::RenderTarget;

				targets.m_Color = builder.ImportTexture("MainView.BackBuffer",
					backTexture,
					backBufferDesc,
					D3D12_RESOURCE_STATE_PRESENT);

				// Create depth buffer
				RGTextureDesc depthBufferDesc{};
				depthBufferDesc.m_Width = width;
				depthBufferDesc.m_Height = height;
				depthBufferDesc.m_Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
				depthBufferDesc.m_Usage = RGTextureUsage::DepthStencil;

				targets.m_Depth = builder.CreateTexture("MainView.DepthBuffer", depthBufferDesc);

				// BackBuffer ResourceIndex
				const ResourceIndex backBufferResourceIndex = rg.GetResourceIndex(targets.m_Color);
				GGLAB_ASSERT_MSG(ExternalResourceIndex::IsExternal(backBufferResourceIndex),
					"BackBuffer must be external ResourceIndex.");
				targets.m_BackBufferResourceIndex = backBufferResourceIndex;

				// RTVview Key
				targets.m_BackBufferRTVKey = DX12ViewCache::BuildKey<ViewType::RTV>(
					backBufferResourceIndex, backTexture);

				rg.GetViewCache()->GetOrCreate(targets.m_BackBufferRTVKey, backTexture);
			});

		// SwapChain prepare backbuffer
		struct PrepareBackBufferData
		{
			RGTextureId m_BackBuffer{};
			ViewKey m_RtvKey{};
		};

		rg.AddPass<PrepareBackBufferData>("SwapChain.PrepareBackBuffer",
			[](RenderGraph::RGBuilder& builder, PrepareBackBufferData& data)
			{
				//GGLAB_LOG_GRAPHICS_INFO("SwapChain.PrepareBackBuffer(Setup)");
				builder.SideEffect();

				auto& targetsTable = builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName);
				auto& targets = targetsTable.GetViewTargets(RenderViewID::Main);

				data.m_BackBuffer = builder.Write(targets.m_Color,
					RGTextureUsage::RenderTarget);
				data.m_RtvKey = targets.m_BackBufferRTVKey;
			},
			[&rg, swapChain](RGExecuteContext& executeContext, PrepareBackBufferData& data)
			{
				auto* backTexture = rg.GetTexture(data.m_BackBuffer);
				GGLAB_ASSERT(backTexture);

				auto* commandList = executeContext.m_GraphicsCommandList;

				// TODO: RenderGraph resource auto barrier
				CD3DX12_TEXTURE_BARRIER barrier(
					D3D12_BARRIER_SYNC_ALL,
					D3D12_BARRIER_SYNC_RENDER_TARGET,
					D3D12_BARRIER_ACCESS_COMMON,
					D3D12_BARRIER_ACCESS_RENDER_TARGET,
					D3D12_BARRIER_LAYOUT_PRESENT,
					D3D12_BARRIER_LAYOUT_RENDER_TARGET,
					backTexture->Get(),
					CD3DX12_BARRIER_SUBRESOURCE_RANGE(0));
				commandList->AddTextureBarrier(barrier);
				commandList->FlushBarriers();

				auto* viewCache = rg.GetViewCache();
				const auto& rtv = viewCache->GetOrCreate(data.m_RtvKey, backTexture);

				commandList->ClearRenderTarget(rtv, swapChain->GetClearColor());

			});

		// IBL Pass
		m_IBLPass.AddPass(rg, context, services);

		// RenderPass ForwardPBR
		m_ForwardPBRPass.AddPass(rg, context, services);

		// IBL Debug Preview
		m_IBLDebugPreviewPass.AddPass(rg, context, services);

		// DevelopGui	
		m_DevelopGuiPass.AddPass(rg, context, services);

		// Finish backbuffer
		struct FinishBackBufferData
		{
			RGTextureId m_BackBuffer{};
		};
		rg.AddPass<FinishBackBufferData>("SwapChain.FinishBackBuffer",
			[](RenderGraph::RGBuilder& builder, FinishBackBufferData& data)
			{
				//GGLAB_LOG_GRAPHICS_INFO("SwapChain.FinishBackBuffer(Setup)");
				builder.SideEffect();

				auto& targetsTable = builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName);
				auto& targets = targetsTable.GetViewTargets(RenderViewID::Main);
				data.m_BackBuffer = builder.Write(targets.m_Color,
					RGTextureUsage::RenderTarget);
			},
			[&rg](RGExecuteContext& executeContext, FinishBackBufferData& data)
			{
				auto* backTexture = rg.GetTexture(data.m_BackBuffer);
				GGLAB_ASSERT(backTexture);

				auto* commandList = executeContext.m_GraphicsCommandList;

				CD3DX12_TEXTURE_BARRIER barrier(
					D3D12_BARRIER_SYNC_RENDER_TARGET,
					D3D12_BARRIER_SYNC_ALL,
					D3D12_BARRIER_ACCESS_RENDER_TARGET,
					D3D12_BARRIER_ACCESS_COMMON,
					D3D12_BARRIER_LAYOUT_RENDER_TARGET,
					D3D12_BARRIER_LAYOUT_PRESENT,
					backTexture->Get(),
					CD3DX12_BARRIER_SUBRESOURCE_RANGE(0));

				commandList->AddTextureBarrier(barrier);
				commandList->FlushBarriers();
			});
	}
}
