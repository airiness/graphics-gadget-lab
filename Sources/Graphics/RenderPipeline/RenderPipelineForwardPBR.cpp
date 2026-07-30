#include "Core/Precompiled.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPass/ShadowGraphResources.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/RHI/RHITextureViewDescUtils.h"
#include "Graphics/Shader/ShaderManager.h"

namespace gglab
{
	namespace
	{
		struct DisplayViewSetupPassData {};
		struct ShadowSetupPassData {};

		struct PrepareBackBufferPassData
		{
			RGTextureId m_BackBuffer{};
			RGTextureViewId m_Rtv{};
		};

		struct FinishBackBufferPassData {};
	}

	void RenderPipelineForwardPBR::BuildRenderGraph(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(context.IsValid(), "RenderFrameContext invalid.");
		GGLAB_ASSERT_MSG(services.IsValid(), "RenderServices invalid.");

		auto* renderer = services.m_Renderer;
		auto* swapChain = renderer->GetSwapChain();

		const uint32_t frameBackBufferIndex = context.m_BackBufferIndex;

		const RenderViewID displayViewId = context.GetDisplayViewId();
		const DepthConvention displayDepthConvention =
			context.GetDisplayRenderView().m_DepthConvention;
		const uint32_t targetWidth =
			swapChain->GetBufferWidth();
		const uint32_t targetHeight =
			swapChain->GetBufferHeight();

		PrepareForwardPasses(services);
		const DepthCoverageFramePlan depthCoverageFramePlan =
			BuildDepthCoverageFramePlanForFrame(
				context,
				targetWidth,
				targetHeight);
		if (depthCoverageFramePlan.UsesForwardDepthWrite())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"Depth coverage frame uses Forward-write fallback: {}",
				depthCoverageFramePlan.m_Diagnostic);
		}
		else if (!depthCoverageFramePlan.RendersGeometry())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Depth coverage frame rejected scene geometry: {}",
				depthCoverageFramePlan.m_Diagnostic);
		}

		// DisplayView Setup
		rg.AddPass<DisplayViewSetupPassData>("DisplayView.Setup",
			[swapChain,
				frameBackBufferIndex,
				displayViewId,
				displayDepthConvention,
				depthCoverageFramePlan](
				RenderGraph::RGBuilder& builder,
				DisplayViewSetupPassData&)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& targetsTable = blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& targets = targetsTable.GetViewTargets(displayViewId);
				blackboard.GetOrCreate<DepthCoverageFramePlan>(
					DepthCoverageFramePlanName) =
					depthCoverageFramePlan;

				const uint32_t width = swapChain->GetBufferWidth();
				const uint32_t height = swapChain->GetBufferHeight();

				targets.m_Width = width;
				targets.m_Height = height;

				const RHITextureHandle backTexture = swapChain->GetBackBufferHandle(frameBackBufferIndex);
				GGLAB_ASSERT(backTexture.IsValid());

				// Create HDR scene color
				RHITextureDesc sceneColorDesc{};
				sceneColorDesc.m_Extent = { width, height, 1u };
				sceneColorDesc.m_Format = RHIFormat::R16G16B16A16Float;
				targets.m_SceneColor = builder.CreateTexture("DisplayView.SceneColor", sceneColorDesc);

				// Import backbuffer
				RHITextureDesc backBufferDesc{};
				backBufferDesc.m_Extent = { width, height, 1u };
				backBufferDesc.m_Format = swapChain->GetFormat();

				targets.m_BackBuffer = builder.ImportTexture("DisplayView.BackBuffer",
					backTexture,
					backBufferDesc,
					RGTextureAccess::Present);

				// Create depth buffer
				RHITextureDesc depthBufferDesc{};
				depthBufferDesc.m_Extent = { width, height, 1u };
				depthBufferDesc.m_Format = RHIFormat::R32Typeless;
				depthBufferDesc.m_ClearValue = RHIClearValue{
					.m_Format = RHIFormat::D32Float,
					.m_Depth = screen_space::GetDepthBackgroundValue(
						displayDepthConvention),
					.m_IsDepthStencil = true,
				};
				GGLAB_ASSERT_MSG(
					sceneColorDesc.m_Extent.m_Width ==
						depthBufferDesc.m_Extent.m_Width &&
					sceneColorDesc.m_Extent.m_Height ==
						depthBufferDesc.m_Extent.m_Height &&
					sceneColorDesc.m_Extent.m_Depth ==
						depthBufferDesc.m_Extent.m_Depth &&
					sceneColorDesc.m_SampleCount ==
						depthBufferDesc.m_SampleCount,
					"Display view color and depth targets must have matching extents and sample counts.");

				auto& sceneDepth = blackboard.GetOrCreate<RGSceneDepthResources>(
					SceneDepthResourcesName);
				sceneDepth.m_Texture = builder.CreateTexture(
					"DisplayView.DepthBuffer",
					depthBufferDesc);
				sceneDepth.m_DsvDesc = MakeRHITexture2DViewDesc(
					RHIFormat::D32Float,
					0,
					1,
					RHITextureAspect::Depth);
				sceneDepth.m_DsvDesc.m_Type = RHITextureViewType::DepthStencil;
				sceneDepth.m_SrvDesc = MakeRHITexture2DViewDesc(
					RHIFormat::R32Float,
					0,
					1,
					RHITextureAspect::Depth);
				sceneDepth.m_Convention = displayDepthConvention;

			});

		// Shadow Setup
		rg.AddPass<ShadowSetupPassData>("ShadowMap.Setup",
			[renderer, &context](RenderGraph::RGBuilder& builder, ShadowSetupPassData&)
			{
				const auto& shadowSettings = context.GetDirectionalShadowSettings();
				auto& shadowRes = builder.GetBlackboard().GetOrCreate<RGShadowResources>(ShadowResourcesName);
				shadowRes.m_ShadowMapSize = std::max(shadowSettings.m_ShadowMapSize, 1u);

				RHITextureDesc shadowMapDesc{};
				shadowMapDesc.m_Extent = { shadowRes.m_ShadowMapSize, shadowRes.m_ShadowMapSize, 1u };
				shadowMapDesc.m_Format = RHIFormat::R32Typeless;
				shadowRes.m_DirectionalShadowMap = builder.CreateTexture(
					"Shadow.DirectionalShadowMap",
					shadowMapDesc);

				shadowRes.m_ShadowMapPreviewSize = DefaultDirectionalShadowMapPreviewSize;
				auto* renderResourceRegistry = renderer->GetRenderResourceRegistry();
				GGLAB_ASSERT_NOT_NULL(renderResourceRegistry);
				renderResourceRegistry->EnsureShadowPreviewResources(shadowRes.m_ShadowMapPreviewSize);

				const auto* shadowMapPreviewDesc = renderResourceRegistry->GetTextureDesc(
					RenderResourceRegistry::TextureIndex::Preview_Shadow_DirectionalShadowMap);
				GGLAB_ASSERT_NOT_NULL(shadowMapPreviewDesc);

				shadowRes.m_DirectionalShadowMapPreview = builder.ImportTexture(
					"Shadow.DirectionalShadowMapPreview",
					renderResourceRegistry->GetTextureHandle(
						RenderResourceRegistry::TextureIndex::Preview_Shadow_DirectionalShadowMap),
					*shadowMapPreviewDesc,
					RGTextureAccess::None);
			});

		// SwapChain prepare backbuffer
		rg.AddPass<PrepareBackBufferPassData>("SwapChain.PrepareBackBuffer",
			[displayViewId](RenderGraph::RGBuilder& builder, PrepareBackBufferPassData& data)
			{
				builder.SideEffect();

				auto& targetsTable = builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName);
				auto& targets = targetsTable.GetViewTargets(displayViewId);

				builder.WriteInPlace(targets.m_BackBuffer,
					RGTextureAccess::RenderTarget);
				data.m_BackBuffer = targets.m_BackBuffer;
				data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(data.m_BackBuffer);
			},
			[renderer](RGExecuteContext& executeContext, PrepareBackBufferPassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				commandContext->ClearColor(rtv, renderer->GetBackBufferClearColor());
			});

		// IBL Pass
		m_IBLPass.AddPass(rg, context, services);

		// Directional Shadow Map
		m_DirectionalShadowMapPass.AddPass(rg, context, services);

		// ShadowMap Preview
		m_ShadowMapPreviewPass.AddPass(rg, context, services);

		// Clear HDR color before background and scene geometry.
		m_ClearViewTargetsPass.AddPass(rg, context, services);

		if (depthCoverageFramePlan.UsesDepthPrepassEqual())
		{
			m_DepthPrepassPass.AddPass(
				rg,
				context,
				services);
			m_SkyboxPass.AddPass(rg, context, services);
			m_ForwardOpaquePass.AddPass(
				rg,
				context,
				services);
		}
		else if (depthCoverageFramePlan.UsesForwardDepthWrite())
		{
			m_ForwardOpaquePass.AddPass(
				rg,
				context,
				services);
			m_SkyboxPass.AddPass(rg, context, services);
		}
		else
		{
			// Preserve a defined background depth while rejecting unsafe
			// scene draw packets for this frame.
			m_DepthPrepassPass.AddPass(
				rg,
				context,
				services);
			m_SkyboxPass.AddPass(rg, context, services);
		}

		if (depthCoverageFramePlan.RendersGeometry())
		{
			m_ForwardTransparentPass.AddPass(
				rg,
				context,
				services);
		}

		// Depth-tested world-space debug geometry is part of HDR scene color.
		m_DebugDrawScenePass.AddPass(rg, context, services);

		if (context.IsRenderSceneReady())
		{
			// Final color requires View StructuredBuffer data.
			m_PostProcessPipeline.AddPasses(rg, context, services);
		}

		// IBL Preview
		m_IBLPreviewPass.AddPass(rg, context, services);

		// Always-visible debug geometry is composed after all back-buffer previews.
		m_DebugDrawOverlayPass.AddPass(rg, context, services);

		// DevelopGui	
		m_DevelopGuiPass.AddPass(rg, context, services);

		// Return persistent IBL resources to Common only after every consumer and
		// preview pass has declared its final access for this frame.
		m_IBLPass.AddFinishPass(rg);

		// Finish backbuffer
		rg.AddPass<FinishBackBufferPassData>("SwapChain.FinishBackBuffer",
			[displayViewId](RenderGraph::RGBuilder& builder, FinishBackBufferPassData&)
			{
				builder.SideEffect();

				auto& targetsTable = builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName);
				auto& targets = targetsTable.GetViewTargets(displayViewId);
				builder.Export(
					targets.m_BackBuffer,
					RGTextureAccess::Present,
					RHISubresourceRange
					{
						.m_MipCount = 1,
						.m_ArraySliceCount = 1,
						.m_Aspects = RHITextureAspect::Color,
					});
			});
	}

	void RenderPipelineForwardPBR::PrepareForwardPasses(
		const RenderServices& services) noexcept
	{
		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_ForwardPBRShaderSet.IsValid())
		{
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath =
				L"Passes/PassForwardCoverage.hlsl";
			shaderDesc.m_Stage = ShaderStage::Vertex;
			shaderDesc.m_Entry = L"VSMain";
			m_ForwardPBRShaderSet.m_CoverageVertexShader =
				shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_SourcePath =
				L"Passes/PassForwardPBR.hlsl";
			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSMain";
			m_ForwardPBRShaderSet.m_ShadingPixelShader =
				shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_SourcePath =
				L"Passes/PassDepthPrepass.hlsl";
			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSAlphaTest";
			m_ForwardPBRShaderSet.m_AlphaTestPixelShader =
				shaderManager->LoadShader(shaderDesc);
		}

		GGLAB_ASSERT_MSG(
			m_ForwardPBRShaderSet.IsValid(),
			"Forward renderer failed to prepare its shared shader set.");
		m_DepthPrepassPass.Prepare(
			services,
			m_ForwardPBRShaderSet);
		m_ForwardOpaquePass.Prepare(
			services,
			m_ForwardPBRShaderSet);
		m_ForwardTransparentPass.Prepare(
			services,
			m_ForwardPBRShaderSet);
	}

	DepthCoverageFramePlan
		RenderPipelineForwardPBR::
			BuildDepthCoverageFramePlanForFrame(
				const RenderFrameContext& context,
				uint32_t targetWidth,
				uint32_t targetHeight) const
	{
		const RenderViewID displayViewId =
			context.GetDisplayViewId();
		const RenderQueue& renderQueue =
			context.GetRenderQueue(displayViewId);
		DepthCoverageFramePlanBuildInfo buildInfo{
			.m_RenderQueue = std::addressof(renderQueue),
			.m_ExpectedViewId = displayViewId,
			.m_TargetWidth = targetWidth,
			.m_TargetHeight = targetHeight,
			.m_DepthConvention =
				context.GetDisplayRenderView().
					m_DepthConvention,
			.m_ShadingPipelinesAvailable =
				m_ForwardPBRShaderSet.IsValid(),
		};

		if (buildInfo.m_ShadingPipelinesAvailable)
		{
			for (const DrawItem& drawItem :
				renderQueue.m_DrawItems)
			{
				if (drawItem.m_Bucket !=
						RenderBucket::Opaque &&
					drawItem.m_Bucket !=
						RenderBucket::AlphaTest)
				{
					continue;
				}
				if ((drawItem.m_VariantBits &
						~RenderQueueBuilder::VariantMask) != 0 ||
					RenderQueueBuilder::DecodeVariantBucket(
						drawItem.m_VariantBits) !=
						drawItem.m_Bucket)
				{
					continue;
				}
				const uint64_t variantBits =
					drawItem.m_VariantBits &
						RenderQueueBuilder::VariantMask;
				const size_t variantIndex =
					static_cast<size_t>(variantBits);
				if (!buildInfo.m_PrepassPipelineSignatures[
					variantIndex])
				{
					buildInfo.m_PrepassPipelineSignatures[
						variantIndex] =
							m_DepthPrepassPass.
								DescribePipelineVariant(
									variantBits).
							m_LogicalMetadata.
							m_DepthCoveragePipelineSignature;
					buildInfo.m_ForwardPipelineSignatures[
						variantIndex] =
							m_ForwardOpaquePass.
								DescribePipelineVariant(
									variantBits).
							m_LogicalMetadata.
							m_DepthCoveragePipelineSignature;
				}
			}
		}

		return BuildDepthCoverageFramePlan(buildInfo);
	}
}
