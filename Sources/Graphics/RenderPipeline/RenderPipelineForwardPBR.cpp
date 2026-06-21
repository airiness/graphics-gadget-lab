#include "Core/Precompiled.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Graphics/Renderer.h"
#include "Graphics/DX12/DX12Device.h"
#include "Graphics/DX12/DX12SwapChain.h"
#include "Graphics/RenderGraph/RGFrameTargets.h"
#include "Graphics/RenderGraph/RGShadowResources.h"
#include "Graphics/RenderGraph/RGResourceUtils.h"
#include "Graphics/RenderResourceRegistry.h"

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

				// Create HDR scene color
				RGTextureDesc sceneColorDesc{};
				sceneColorDesc.m_Width = width;
				sceneColorDesc.m_Height = height;
				sceneColorDesc.m_Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
				sceneColorDesc.m_Usage = RGTextureUsage::RenderTarget | RGTextureUsage::Sample;
				targets.m_SceneColor = builder.CreateTexture("MainView.SceneColor", sceneColorDesc);

				// Import backbuffer
				RGTextureDesc backBufferDesc{};
				backBufferDesc.m_Width = width;
				backBufferDesc.m_Height = height;
				backBufferDesc.m_Format = swapChain->GetFormat();
				backBufferDesc.m_Usage = RGTextureUsage::RenderTarget;

				targets.m_BackBuffer = builder.ImportTexture("MainView.BackBuffer",
					backTexture,
					backBufferDesc,
					RGTextureUsage::Present);

				// Create depth buffer
				RGTextureDesc depthBufferDesc{};
				depthBufferDesc.m_Width = width;
				depthBufferDesc.m_Height = height;
				depthBufferDesc.m_Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
				depthBufferDesc.m_Usage = RGTextureUsage::DepthStencil;

				targets.m_Depth = builder.CreateTexture("MainView.DepthBuffer", depthBufferDesc);

				// BackBuffer ResourceIndex
				const ResourceIndex backBufferResourceIndex = rg.GetResourceIndex(targets.m_BackBuffer);
				GGLAB_ASSERT_MSG(ExternalResourceIndex::IsExternal(backBufferResourceIndex),
					"BackBuffer must be external ResourceIndex.");
				targets.m_BackBufferResourceIndex = backBufferResourceIndex;

				// RTVview Key
				targets.m_BackBufferRTVKey = DX12ViewCache::BuildKey<ViewType::RTV>(
					backBufferResourceIndex, backTexture);

				rg.GetViewCache()->GetOrCreate(targets.m_BackBufferRTVKey, backTexture);
			});

		// Shadow Setup
		struct ShadowSetupData {};
		rg.AddPass<ShadowSetupData>("ShadowMap.Setup",
			[renderer, &context](RenderGraph::RGBuilder& builder, ShadowSetupData&)
			{
				const auto& shadowSettings = context.GetDirectionalShadowSettings();
				auto& shadowRes = builder.GetBlackboard().GetOrCreate<RGShadowResources>(ShadowResourcesName);
				shadowRes.m_ShadowMapSize = std::max(shadowSettings.m_ShadowMapSize, 1u);

				RGTextureDesc shadowMapDesc{};
				shadowMapDesc.m_Width = shadowRes.m_ShadowMapSize;
				shadowMapDesc.m_Height = shadowRes.m_ShadowMapSize;
				shadowMapDesc.m_Format = DXGI_FORMAT_R32_TYPELESS;
				shadowMapDesc.m_Usage = RGTextureUsage::DepthStencil | RGTextureUsage::Sample;
				shadowRes.m_DirectionalShadowMap = builder.CreateTexture(
					"Shadow.DirectionalShadowMap",
					shadowMapDesc);

				shadowRes.m_ShadowMapPreviewSize = DefaultDirectionalShadowMapPreviewSize;
				auto* renderResourceRegistry = renderer->GetRenderResourceRegistry();
				GGLAB_ASSERT_NOT_NULL(renderResourceRegistry);
				renderResourceRegistry->EnsureShadowPreviewResources(shadowRes.m_ShadowMapPreviewSize);

				auto* shadowMapPreviewTexture = renderResourceRegistry->GetTexture(
					RenderResourceRegistry::TextureIndex::Preview_Shadow_DirectionalShadowMap);
				GGLAB_ASSERT_NOT_NULL(shadowMapPreviewTexture);

				const auto shadowMapPreviewDesc = ToRGTextureDesc(
					*shadowMapPreviewTexture,
					RGTextureUsage::RenderTarget | RGTextureUsage::Sample);

				shadowRes.m_DirectionalShadowMapPreview = builder.ImportTexture(
					"Shadow.DirectionalShadowMapPreview",
					shadowMapPreviewTexture,
					shadowMapPreviewDesc,
					RGTextureUsage::None);
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

				data.m_BackBuffer = builder.Write(targets.m_BackBuffer,
					RGTextureUsage::RenderTarget);
				data.m_RtvKey = targets.m_BackBufferRTVKey;
			},
			[&rg, swapChain](RGExecuteContext& executeContext, PrepareBackBufferData& data)
			{
				auto* backTexture = rg.GetTexture(data.m_BackBuffer);
				GGLAB_ASSERT(backTexture);

				auto* commandList = executeContext.m_GraphicsCommandList;

				auto* viewCache = rg.GetViewCache();
				const auto& rtv = viewCache->GetOrCreate(data.m_RtvKey, backTexture);

				commandList->ClearRenderTarget(rtv, swapChain->GetClearColor());

			});

		// IBL Pass
		m_IBLPass.AddPass(rg, context, services);

		// Directional Shadow Map
		m_DirectionalShadowMapPass.AddPass(rg, context, services);

		// ShadowMap Preview
		m_ShadowMapPreviewPass.AddPass(rg, context, services);

		// RenderPass ForwardPBR
		m_ForwardPBRPass.AddPass(rg, context, services);

		if (context.IsRenderSceneReady())
		{
			// Tonemap requires View StructuredBuffer data.
			m_TonemapPass.AddPass(rg, context, services);
		}

		// IBL Preview
		m_IBLPreviewPass.AddPass(rg, context, services);

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
				data.m_BackBuffer = builder.Write(targets.m_BackBuffer,
					RGTextureUsage::RenderTarget);
				builder.Export(data.m_BackBuffer, RGTextureUsage::Present);
			});
	}
}
