#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Core/CoreMacros.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/Pipeline/ForwardPlus.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RenderPipeline/RenderPipelineOverlayExtensionBase.h"
#include "Graphics/RenderPass/ForwardPlusGraphResources.h"
#include "Graphics/RenderPass/GTAOGraphResources.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPass/ShadowGraphResources.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/RHI/RHITextureViewDescUtils.h"
#include "Graphics/Shader/ShaderManager.h"

#include <algorithm>
#include <memory>
#include <span>

namespace gglab
{
	namespace
	{
		struct DisplayViewSetupPassData
		{
		};
		struct ShadowSetupPassData
		{
		};

		struct PrepareBackBufferPassData
		{
			RGTextureId m_BackBuffer{};
			RGTextureViewId m_Rtv{};
		};

		struct FinishBackBufferPassData
		{
		};
	}

	void RenderPipelineForwardPBR::BuildRenderGraph(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(context.IsValid(), "RenderFrameContext invalid.");
		GGLAB_ASSERT_MSG(services.IsValid(), "RenderServices invalid.");

		auto* renderer = services.m_Renderer;
		auto* swapChain = renderer->GetSwapChain();

		const uint32_t frameBackBufferIndex = context.m_BackBufferIndex;

		const RenderViewID displayViewId = context.GetDisplayViewId();
		const DepthConvention displayDepthConvention =
			context.GetDisplayRenderView().m_DepthConvention;
		const uint32_t targetWidth = swapChain->GetBufferWidth();
		const uint32_t targetHeight = swapChain->GetBufferHeight();
		const ForwardPlusSettings& forwardPlusSettings =
			context.GetDisplayViewRenderSettings().m_Lighting.m_ForwardPlus;
		const bool forwardPlusEnabled =
			forwardPlusSettings.m_Mode == ForwardLightingMode::ForwardPlus;
		const bool forwardPlusAvailable = forwardPlusEnabled &&
			IsForwardPlusGlobalLightCountSupported(
				static_cast<uint32_t>(context.m_RenderScene.m_GlobalLightIndices.size()));
		PrepareForwardPasses(services);
		const DepthCoverageFramePlan depthCoverageFramePlan =
			BuildDepthCoverageFramePlanForFrame(context, targetWidth, targetHeight);
		ForwardPlusFrameStatus forwardPlusStatus = ForwardPlusFrameStatus::Disabled;
		if (forwardPlusEnabled)
		{
			if (!forwardPlusAvailable)
			{
				forwardPlusStatus = ForwardPlusFrameStatus::GlobalLightCapacityExceeded;
			}
			else if (!context.IsRenderSceneReady())
			{
				forwardPlusStatus = ForwardPlusFrameStatus::RenderSceneUnavailable;
			}
			else if (!depthCoverageFramePlan.UsesDepthPrepassEqual())
			{
				forwardPlusStatus = ForwardPlusFrameStatus::DepthCoverageUnavailable;
			}
			else if (!depthCoverageFramePlan.m_HasDepthCoverageDraws)
			{
				forwardPlusStatus = ForwardPlusFrameStatus::NoOpaqueDraws;
			}
			else
			{
				forwardPlusStatus = ForwardPlusFrameStatus::Active;
			}
		}
		const bool forwardPlusActive = forwardPlusStatus == ForwardPlusFrameStatus::Active;
		const bool forwardPlusValidationEnabled =
			forwardPlusActive && forwardPlusSettings.m_EnableHdrDiffValidation &&
			m_ForwardPlusValidationPass.IsAvailable();
		const GTAOSettings& gtaoSettings =
			context.GetDisplayViewRenderSettings().m_Lighting.m_GTAO;
		m_GTAOPass.Prepare(services);
		const GTAOFrameStatus gtaoStatus = ResolveGTAOFrameStatus(gtaoSettings.m_Enabled,
			m_GTAOPass.GetCapabilityStatus().IsCoreAvailable(), m_GTAOPass.IsAvailable(),
			context.IsRenderSceneReady(), depthCoverageFramePlan.UsesDepthPrepassEqual(),
			depthCoverageFramePlan.m_HasDepthCoverageDraws);
		const bool gtaoActive = gtaoStatus == GTAOFrameStatus::Active;

		auto& forwardPlusResources =
			rg.GetBlackboard().Create<RGForwardPlusResources>(ForwardPlusResourcesName);
		auto& gtaoResources =
			rg.GetBlackboard().Create<RGGTAOResources>(GTAOResourcesName);
		gtaoResources.m_Status = gtaoStatus;
		gtaoResources.m_Capabilities = m_GTAOPass.GetCapabilityStatus();
		gtaoResources.m_ResolvedSettings = gtaoSettings;
		forwardPlusResources.m_Status = forwardPlusStatus;
		forwardPlusResources.m_LightBaseIndex = context.m_RenderScene.m_LightBaseIndex;
		forwardPlusResources.m_LightTableCapacity = context.m_RenderScene.m_LightCount;
		forwardPlusResources.m_DirectionalLightCount =
			context.m_RenderScene.m_DirectionalLightCount;
		forwardPlusResources.m_LocalLightCount = context.m_RenderScene.m_LocalLightCount;
		forwardPlusResources.m_LightTypesByIndex = context.m_RenderScene.m_LightTypesByIndex;
		forwardPlusResources.m_DebugReadback = m_ForwardPlusDebugReadback;

		if (forwardPlusActive)
		{
			m_ForwardPlusCullPass.Prepare(services);
		}
		if (forwardPlusValidationEnabled)
		{
			m_ForwardPlusValidationPass.Prepare(services);
		}
		if (depthCoverageFramePlan.UsesForwardDepthWrite())
		{
			GGLAB_LOG_GRAPHICS_WARN("Depth coverage frame uses Forward-write fallback: {}",
				depthCoverageFramePlan.m_Diagnostic);
		}
		else if (!depthCoverageFramePlan.RendersGeometry())
		{
			GGLAB_LOG_GRAPHICS_ERROR("Depth coverage frame rejected scene geometry: {}",
				depthCoverageFramePlan.m_Diagnostic);
		}

		// DisplayView Setup
		rg.AddPass<DisplayViewSetupPassData>("DisplayView.Setup",
			[swapChain, frameBackBufferIndex, displayViewId, displayDepthConvention,
			depthCoverageFramePlan](RenderGraph::RGBuilder& builder, DisplayViewSetupPassData&)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& targetsTable =
					blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& targets = targetsTable.GetViewTargets(displayViewId);
				blackboard.GetOrCreate<DepthCoverageFramePlan>(DepthCoverageFramePlanName) =
					depthCoverageFramePlan;

				const uint32_t width = swapChain->GetBufferWidth();
				const uint32_t height = swapChain->GetBufferHeight();

				targets.m_Width = width;
				targets.m_Height = height;

				const RHITextureHandle backTexture =
					swapChain->GetBackBufferHandle(frameBackBufferIndex);
				GGLAB_ASSERT(backTexture.IsValid());

				// Create HDR scene color
				RHITextureDesc sceneColorDesc{};
				sceneColorDesc.m_Extent = { width, height, 1u };
				sceneColorDesc.m_Format = RHIFormat::R16G16B16A16Float;
				targets.m_SceneColor =
					builder.CreateTexture("DisplayView.SceneColor", sceneColorDesc);

				// Import backbuffer
				RHITextureDesc backBufferDesc{};
				backBufferDesc.m_Extent = { width, height, 1u };
				backBufferDesc.m_Format = swapChain->GetFormat();

				targets.m_BackBuffer = builder.ImportTexture("DisplayView.BackBuffer", backTexture,
					backBufferDesc, RGTextureAccess::Present, RGContentValidity::Undefined);

				// Create depth buffer
				RHITextureDesc depthBufferDesc{};
				depthBufferDesc.m_Extent = { width, height, 1u };
				depthBufferDesc.m_Format = RHIFormat::R32Typeless;
				depthBufferDesc.m_ClearValue = RHIClearValue{
					.m_Format = RHIFormat::D32Float,
					.m_Depth = screen_space::GetDepthBackgroundValue(displayDepthConvention),
					.m_IsDepthStencil = true,
				};
				GGLAB_ASSERT_MSG(
					sceneColorDesc.m_Extent.m_Width == depthBufferDesc.m_Extent.m_Width &&
					sceneColorDesc.m_Extent.m_Height == depthBufferDesc.m_Extent.m_Height &&
					sceneColorDesc.m_Extent.m_Depth == depthBufferDesc.m_Extent.m_Depth &&
					sceneColorDesc.m_SampleCount == depthBufferDesc.m_SampleCount,
					"Display view color and depth targets must have matching extents and sample counts.");

				auto& sceneDepth =
					blackboard.GetOrCreate<RGSceneDepthResources>(SceneDepthResourcesName);
				sceneDepth.m_Texture =
					builder.CreateTexture("DisplayView.DepthBuffer", depthBufferDesc);
				sceneDepth.m_DsvDesc =
					MakeRHITexture2DViewDesc(RHIFormat::D32Float, 0, 1, RHITextureAspect::Depth);
				sceneDepth.m_DsvDesc.m_Type = RHITextureViewType::DepthStencil;
				sceneDepth.m_SrvDesc =
					MakeRHITexture2DViewDesc(RHIFormat::R32Float, 0, 1, RHITextureAspect::Depth);
				sceneDepth.m_Convention = displayDepthConvention;
			});

		// Shadow Setup
		rg.AddPass<ShadowSetupPassData>("ShadowMap.Setup",
			[renderer, &context](RenderGraph::RGBuilder& builder, ShadowSetupPassData&)
			{
				const auto& shadowSettings = context.GetDirectionalShadowSettings();
				auto& shadowRes =
					builder.GetBlackboard().GetOrCreate<RGShadowResources>(ShadowResourcesName);
				shadowRes.m_ShadowMapSize = std::max(shadowSettings.m_ShadowMapSize, 1u);

				RHITextureDesc shadowMapDesc{};
				shadowMapDesc.m_Extent = { shadowRes.m_ShadowMapSize, shadowRes.m_ShadowMapSize, 1u };
				shadowMapDesc.m_Format = RHIFormat::R32Typeless;
				shadowRes.m_DirectionalShadowMap =
					builder.CreateTexture("Shadow.DirectionalShadowMap", shadowMapDesc);

				shadowRes.m_ShadowMapPreviewSize = DefaultDirectionalShadowMapPreviewSize;
				auto* renderResourceRegistry = renderer->GetRenderResourceRegistry();
				GGLAB_ASSERT_NOT_NULL(renderResourceRegistry);
				renderResourceRegistry->EnsureShadowPreviewResources(
					shadowRes.m_ShadowMapPreviewSize);

				const auto* shadowMapPreviewDesc = renderResourceRegistry->GetTextureDesc(
					RenderResourceRegistry::TextureIndex::Preview_Shadow_DirectionalShadowMap);
				GGLAB_ASSERT_NOT_NULL(shadowMapPreviewDesc);

				shadowRes.m_DirectionalShadowMapPreview = builder.ImportTexture(
					"Shadow.DirectionalShadowMapPreview",
					renderResourceRegistry->GetTextureHandle(
						RenderResourceRegistry::TextureIndex::Preview_Shadow_DirectionalShadowMap),
					*shadowMapPreviewDesc, RGTextureAccess::None, RGContentValidity::Defined);
			});

		// SwapChain prepare backbuffer
		rg.AddPass<PrepareBackBufferPassData>(
			"SwapChain.PrepareBackBuffer",
			[displayViewId](RenderGraph::RGBuilder& builder, PrepareBackBufferPassData& data)
			{
				builder.SideEffect();

				auto& targetsTable =
					builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName);
				auto& targets = targetsTable.GetViewTargets(displayViewId);

				builder.WriteInPlace(targets.m_BackBuffer, RGTextureAccess::RenderTarget);
				data.m_BackBuffer = targets.m_BackBuffer;
				data.m_Rtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_BackBuffer);
			},
			[renderer](RGExecuteContext& executeContext, PrepareBackBufferPassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				const RHIRenderingAttachment colorAttachment{
					.m_View = rtv,
					.m_LoadOp = RHIContentLoadOp::DontCare,
				};
				commandContext->BeginRendering({ .m_ColorAttachments =
					std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
				commandContext->ClearColorAttachment(0, renderer->GetBackBufferClearColor());
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
			m_DepthPrepassPass.AddPass(rg, context, services);
			if (forwardPlusActive)
			{
				m_ForwardPlusCullPass.AddPass(rg, context, services);
			}
			if (gtaoActive)
			{
				m_GTAOPass.AddPass(rg, context, services);
			}
			m_SkyboxPass.AddPass(rg, context, services);
			if (depthCoverageFramePlan.AddsForwardOpaquePass())
			{
				m_ForwardOpaquePass.AddPass(rg, context, services);
				if (forwardPlusValidationEnabled)
				{
					m_ForwardPlusValidationPass.AddPass(rg, context, services);
				}
			}
		}
		else if (depthCoverageFramePlan.UsesForwardDepthWrite())
		{
			if (depthCoverageFramePlan.AddsForwardOpaquePass())
			{
				m_ForwardOpaquePass.AddPass(rg, context, services);
			}
			m_SkyboxPass.AddPass(rg, context, services);
		}
		else
		{
			// Preserve a defined background depth while rejecting unsafe
			// scene draw packets for this frame.
			m_DepthPrepassPass.AddPass(rg, context, services);
			m_SkyboxPass.AddPass(rg, context, services);
		}

		// Opaque extensions contribute to the main HDR color and depth before
		// transparent and post-processing consumers.
		if (m_SceneExtension)
		{
			m_SceneExtension->AddOpaqueScenePasses(rg, context, services);
		}

		if (depthCoverageFramePlan.AddsForwardTransparentPass())
		{
			m_ForwardTransparentPass.AddPass(rg, context, services);
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

		if (services.m_OverlayExtension)
		{
			services.m_OverlayExtension->AddOverlayPasses(rg, context, services);
		}

		// Return persistent IBL resources to Common only after every consumer and
		// preview pass has declared its final access for this frame.
		m_IBLPass.AddFinishPass(rg);

		// Finish backbuffer
		rg.AddPass<FinishBackBufferPassData>("SwapChain.FinishBackBuffer",
			[displayViewId](RenderGraph::RGBuilder& builder, FinishBackBufferPassData&)
			{
				builder.SideEffect();

				auto& targetsTable =
					builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName);
				auto& targets = targetsTable.GetViewTargets(displayViewId);
				builder.Export(targets.m_BackBuffer, RGTextureAccess::Present,
					RHISubresourceRange{
						.m_MipCount = 1,
						.m_ArraySliceCount = 1,
						.m_Aspects = RHITextureAspect::Color,
					});
			});
	}

	void RenderPipelineForwardPBR::PrepareForwardPasses(const RenderServices& services) noexcept
	{
		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_ForwardPBRShaderSet.IsValid())
		{
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Passes/PassForwardCoverage.hlsl";
			shaderDesc.m_Stage = ShaderStage::Vertex;
			shaderDesc.m_Entry = L"VSMain";
			m_ForwardPBRShaderSet.m_CoverageVertexShader = shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_SourcePath = L"Passes/PassForwardPBR.hlsl";
			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSMain";
			m_ForwardPBRShaderSet.m_LegacyShadingPixelShader =
				shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_Defines = {
				{
					.m_Name = L"GGLAB_FORWARD_PLUS",
					.m_Value = L"1",
				},
			};
			m_ForwardPBRShaderSet.m_ForwardPlusShadingPixelShader =
				shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_Defines.push_back({
				.m_Name = L"GGLAB_FORWARD_PLUS_VALIDATION",
				.m_Value = L"1",
				});
			m_ForwardPBRShaderSet.m_ForwardPlusValidationPixelShader =
				shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_Defines = {
				{
					.m_Name = L"GGLAB_GTAO_CONTRIBUTION_OUTPUT",
					.m_Value = L"1",
				},
			};
			m_ForwardPBRShaderSet.m_LegacyGTAOContributionPixelShader =
				shaderManager->LoadShader(shaderDesc);
			shaderDesc.m_Defines.push_back({
				.m_Name = L"GGLAB_FORWARD_PLUS",
				.m_Value = L"1",
				});
			m_ForwardPBRShaderSet.m_ForwardPlusGTAOContributionPixelShader =
				shaderManager->LoadShader(shaderDesc);
			shaderDesc.m_Defines.push_back({
				.m_Name = L"GGLAB_FORWARD_PLUS_VALIDATION",
				.m_Value = L"1",
				});
			m_ForwardPBRShaderSet.m_ForwardPlusValidationGTAOContributionPixelShader =
				shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_SourcePath = L"Passes/PassDepthPrepass.hlsl";
			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSAlphaTest";
			shaderDesc.m_Defines.clear();
			m_ForwardPBRShaderSet.m_AlphaTestPixelShader = shaderManager->LoadShader(shaderDesc);
		}

		if (!m_ForwardPBRShaderSet.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Forward renderer failed to prepare its required shared shader set.");
			GGLAB_UNREACHABLE("Forward renderer production shaders are unavailable.");
		}
		m_DepthPrepassPass.Prepare(services, m_ForwardPBRShaderSet);
		m_ForwardOpaquePass.Prepare(services, m_ForwardPBRShaderSet);
		m_ForwardTransparentPass.Prepare(services, m_ForwardPBRShaderSet);
	}

	DepthCoverageFramePlan RenderPipelineForwardPBR::BuildDepthCoverageFramePlanForFrame(
		const RenderFrameContext& context, uint32_t targetWidth, uint32_t targetHeight) const
	{
		const RenderViewID displayViewId = context.GetDisplayViewId();
		const RenderQueue& renderQueue = context.GetRenderQueue(displayViewId);
		DepthCoverageFramePlanBuildInfo buildInfo{
			.m_RenderQueue = std::addressof(renderQueue),
			.m_ExpectedViewId = displayViewId,
			.m_TargetWidth = targetWidth,
			.m_TargetHeight = targetHeight,
			.m_DepthConvention = context.GetDisplayRenderView().m_DepthConvention,
		};

		for (const RenderBucket bucket : {RenderBucket::Opaque, RenderBucket::AlphaTest})
		{
			for (const bool doubleSided : {false, true})
			{
				const uint64_t variantBits =
					RenderQueueBuilder::EncodeVariantBits(bucket, doubleSided);
				const size_t variantIndex = static_cast<size_t>(variantBits);
				buildInfo.m_PrepassPipelineSignatures[variantIndex] =
					m_DepthPrepassPass.DescribePipelineVariant(variantBits)
					.m_LogicalMetadata.m_DepthCoveragePipelineSignature;
				buildInfo.m_ForwardPipelineSignatures[variantIndex] =
					m_ForwardOpaquePass.DescribePipelineVariant(variantBits)
					.m_LogicalMetadata.m_DepthCoveragePipelineSignature;
			}
		}

		return BuildDepthCoverageFramePlan(buildInfo);
	}
}
