#include "Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlus.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlusDebugReadback.h"
#include "Graphics/Pipeline/TemporalMotion.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineOverlayExtensionBase.h"
#include "Graphics/RenderPass/ForwardPlusGraphResources.h"
#include "Graphics/RenderPass/ForwardPlusValidationGraphResources.h"
#include "Graphics/RenderPass/GTAOGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/ShadowGraphResources.h"
#include "Graphics/RenderPass/TemporalGeometryGraphResources.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <span>
#include <string>

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
		struct ClearMotionVectorsPassData
		{
			RGTextureId m_Motion{};
			RGTextureViewId m_Rtv{};
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

	MaterialDiagnosticPrewarmProgress RenderPipelineForwardPlus::PrewarmMaterialDiagnostics(
		const RenderServices& services, std::span<const uint64_t> drawVariants) noexcept
	{
		PrepareForwardPasses(services, true);
		if (!std::ranges::equal(drawVariants, m_DiagnosticPrewarmDrawVariants))
		{
			m_DiagnosticPrewarmDrawVariants.assign(drawVariants.begin(), drawVariants.end());
			m_DiagnosticPrewarmVariants.clear();
			m_DiagnosticPrewarmProgress = {};
			for (const uint64_t variantBits : drawVariants)
			{
				GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);
				const RenderBucket bucket = RenderQueueBuilder::DecodeVariantBucket(variantBits);
				GGLAB_ASSERT(bucket < RenderBucket::Count);
				if (bucket == RenderBucket::Transparent)
				{
					m_DiagnosticPrewarmVariants.push_back({ .m_DrawVariantBits = variantBits });
					continue;
				}
				// Cover Forward+ opaque shading with and without GTAO output. HDR comparison
				// PSOs exist only when this pipeline owns its readback service.
				for (const bool hdrDiffValidation : { false, true })
				{
					if (hdrDiffValidation && !m_ForwardPlusDebugReadback)
					{
						continue;
					}
					for (const bool contribution : { false, true })
					{
						m_DiagnosticPrewarmVariants.push_back({
							.m_DrawVariantBits = variantBits,
							.m_HdrDiffValidation = hdrDiffValidation,
							.m_GTAOContribution = contribution,
						});
					}
				}
			}
			m_DiagnosticPrewarmProgress.m_TotalCount =
				static_cast<uint32_t>(m_DiagnosticPrewarmVariants.size());
		}
		if (!m_DiagnosticPrewarmProgress.IsReady() && !m_DiagnosticPrewarmProgress.m_Failed)
		{
			const auto& variant = m_DiagnosticPrewarmVariants[m_DiagnosticPrewarmProgress.m_CompletedCount];
			const bool transparent =
				RenderQueueBuilder::DecodeVariantBucket(variant.m_DrawVariantBits) == RenderBucket::Transparent;
			const bool prewarmed = transparent
				? m_ForwardTransparentPass.PrewarmMaterialDiagnosticVariant(services, variant.m_DrawVariantBits)
				: m_ForwardOpaquePass.PrewarmMaterialDiagnosticVariant(services, variant.m_DrawVariantBits,
					variant.m_HdrDiffValidation, variant.m_GTAOContribution);
			if (prewarmed)
			{
				++m_DiagnosticPrewarmProgress.m_CompletedCount;
			}
			else
			{
				m_DiagnosticPrewarmProgress.m_Failed = true;
			}
		}
		return m_DiagnosticPrewarmProgress;
	}

	ResolvedTemporalFramePlan RenderPipelineForwardPlus::ResolveTemporalFramePlan(
		TemporalFramePlanResolveInfo info) const noexcept
	{
		// The Forward shader set requires the depth-prepass velocity programs, so the
		// recipe always provides the depth/velocity path. A lost resolve closure is a
		// frame contract failure in ValidateRenderFrame, not a capability.
		info.m_DepthVelocityPathAvailable = true;
		const SceneExtensionTemporalParticipation participation = m_SceneExtension
			? m_SceneExtension->GetTemporalParticipation()
			: SceneExtensionTemporalParticipation::PostTAA;
		// TemporalIntegrated remains reserved until the extension API can provide matching
		// color, depth, motion, and submitted-frame transaction participation.
		info.m_SceneExtensionParticipation =
			participation == SceneExtensionTemporalParticipation::TemporalIntegrated
			? SceneExtensionTemporalParticipation::TemporalUnsupported
			: participation;
		return gglab::ResolveTemporalFramePlan(info);
	}

	void RenderPipelineForwardPlus::BuildRenderGraph(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(context.IsValid(), "RenderFrameContext invalid.");
		GGLAB_ASSERT_MSG(services.IsValid(), "RenderServices invalid.");

		GGLAB_ASSERT_MSG(m_FramePlan && m_FramePlan->m_FrameSerial == context.m_FrameSerial,
			"Forward+ graph construction requires a Ready validation of the same frame.");
		if (!m_FramePlan || m_FramePlan->m_FrameSerial != context.m_FrameSerial)
		{
			GGLAB_UNREACHABLE("Forward+ graph construction has no validated frame plan.");
		}
		const FramePlan framePlan = std::move(*m_FramePlan);
		m_FramePlan.reset();
		GGLAB_ASSERT_MSG(context.IsRenderSceneReady() && framePlan.m_DepthCoverage.IsValid(),
			"A Ready Forward+ frame has prepared scene data and a valid depth coverage plan.");

		auto* swapChain = services.m_Presentation->GetSwapChain();

		const uint32_t frameBackBufferIndex = context.m_BackBufferIndex;

		const RenderViewID displayViewId = context.GetDisplayViewId();
		const DepthConvention displayDepthConvention =
			context.GetDisplayRenderView().m_DepthConvention;
		const DepthCoverageFramePlan& depthCoverageFramePlan = framePlan.m_DepthCoverage;
		const ForwardPlusFrameStatus forwardPlusStatus = framePlan.m_ForwardPlusStatus;
		const bool forwardPlusActive = forwardPlusStatus == ForwardPlusFrameStatus::Active;
		const GTAOSettings& gtaoSettings =
			context.GetDisplayViewRenderSettings().m_Lighting.m_GTAO;
		const GTAOFrameStatus gtaoStatus = framePlan.m_GTAOStatus;
		const bool gtaoActive = gtaoStatus == GTAOFrameStatus::Active;

		auto& forwardPlusResources =
			rg.GetBlackboard().Create<RGForwardPlusResources>(ForwardPlusResourcesName);
		auto& gtaoResources =
			rg.GetBlackboard().Create<RGGTAOResources>(GTAOResourcesName);
		if (context.GetTemporalFramePlan().HasService(TemporalService::GeometryMotion))
		{
			rg.GetBlackboard().Create<RGTemporalGeometryResources>(
				TemporalGeometryResourcesName);
		}
		gtaoResources.m_Status = gtaoStatus;
		gtaoResources.m_Capabilities = m_GTAOPass.GetCapabilityStatus();
		gtaoResources.m_ResolvedSettings = gtaoSettings;
		forwardPlusResources.m_Status = forwardPlusStatus;
		// Only the Lab-composed validation recipe publishes HDR-diff state; the Lab's
		// readback request selects whether an active Forward+ frame records it.
		bool forwardPlusValidationEnabled = false;
		if (m_ForwardPlusValidationPass.IsAvailable())
		{
			auto& validation = rg.GetBlackboard().Create<RGForwardPlusValidationResources>(
				ForwardPlusValidationResourcesName);
			validation.m_Status = !m_ForwardPlusDebugReadback->IsHdrDiffRequested()
				? ViewRenderFeatureStatus{ ViewRenderFeatureState::Disabled, ViewRenderFeatureReason::NotRequested }
				: !forwardPlusActive
				? ViewRenderFeatureStatus{ ViewRenderFeatureState::Inactive, ViewRenderFeatureReason::RequiredFeatureInactive }
				: ViewRenderFeatureStatus{ ViewRenderFeatureState::Active, ViewRenderFeatureReason::None };
			forwardPlusValidationEnabled = validation.IsActive();
		}
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
		if (context.GetTemporalFramePlan().IsConsumerActive(TemporalConsumer::TemporalAA))
		{
			m_TemporalAAPass.Prepare(services);
		}
		m_AtmospherePass.AddPass(rg, context, services);
		m_AtmospherePass.AddBakePass(rg, services);

		// DisplayView Setup
		rg.AddPass<DisplayViewSetupPassData>("DisplayView.Setup",
			[swapChain, frameBackBufferIndex, displayViewId, displayDepthConvention,
			resolution = context.GetDisplayRenderView().GetResolution(),
			depthCoverageFramePlan,
			geometryMotion =
				context.GetTemporalFramePlan().HasService(TemporalService::GeometryMotion),
			materialDiagnostics = context.m_RenderScene.m_HasMaterialDiagnostics](
				RenderGraph::RGBuilder& builder, DisplayViewSetupPassData&)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& targetsTable =
					blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& targets = targetsTable.GetViewTargets(displayViewId);
				blackboard.GetOrCreate<DepthCoverageFramePlan>(DepthCoverageFramePlanName) =
					depthCoverageFramePlan;

				// Scene targets are render-domain; the back buffer is display-domain, which
				// frame validation matched to the swap chain.
				GGLAB_ASSERT_MSG((resolution.m_Display ==
					ViewExtent{ swapChain->GetBufferWidth(), swapChain->GetBufferHeight() }),
					"The display extent of a validated frame equals the swap-chain extent.");
				GGLAB_ASSERT_MSG(resolution.IsNative(),
					"Render scales below the display extent arrive with temporal upscaling.");
				const uint32_t width = resolution.m_Render.m_Width;
				const uint32_t height = resolution.m_Render.m_Height;

				targets.m_RenderWidth = width;
				targets.m_RenderHeight = height;
				targets.m_DisplayWidth = resolution.m_Display.m_Width;
				targets.m_DisplayHeight = resolution.m_Display.m_Height;

				const RHITextureHandle backTexture =
					swapChain->GetBackBufferHandle(frameBackBufferIndex);
				GGLAB_ASSERT(backTexture.IsValid());

				// Create HDR scene color
				RHITextureDesc sceneColorDesc{};
				sceneColorDesc.m_Extent = { width, height, 1u };
				sceneColorDesc.m_Format = RHIFormat::R16G16B16A16Float;
				targets.m_SceneColor =
					builder.CreateTexture("DisplayView.SceneColor", sceneColorDesc);

				if (materialDiagnostics)
				{
					RHITextureDesc diagnosticColorDesc = sceneColorDesc;
					// Diagnostic attachments clear to zero coverage, including alpha.
					// The optimized clear value must match ClearViewTargets on DX12.
					diagnosticColorDesc.m_ClearValue = RHIClearValue{
						.m_Format = diagnosticColorDesc.m_Format,
						.m_Color = { 0.0f, 0.0f, 0.0f, 0.0f },
					};
					targets.m_MaterialDiagnosticColor =
						builder.CreateTexture("DisplayView.MaterialDiagnosticColor", diagnosticColorDesc);
					targets.m_MaterialDiagnosticLighting =
						builder.CreateTexture("DisplayView.MaterialDiagnosticLighting", diagnosticColorDesc);
					RHITextureDesc coverageDesc = diagnosticColorDesc;
					coverageDesc.m_Format = RHIFormat::R16Float;
					coverageDesc.m_ClearValue->m_Format = coverageDesc.m_Format;
					targets.m_MaterialDiagnosticCoverage =
						builder.CreateTexture("DisplayView.MaterialDiagnosticCoverage", coverageDesc);
				}

				// Import backbuffer
				RHITextureDesc backBufferDesc{};
				backBufferDesc.m_Extent =
					{ resolution.m_Display.m_Width, resolution.m_Display.m_Height, 1u };
				backBufferDesc.m_Format = swapChain->GetFormat();

				targets.m_BackBuffer = builder.ImportTexture("DisplayView.BackBuffer", backTexture,
					backBufferDesc, swapChain->GetBackBufferInitialState(frameBackBufferIndex),
					RGContentValidity::Undefined);

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

				// Display depth of post-temporal composition. At native resolution it is the
				// scene depth itself. The display color is published at the temporal boundary,
				// after the pre-temporal passes that may replace the scene color.
				auto& displayDepth =
					blackboard.GetOrCreate<RGDisplayDepthResources>(DisplayDepthResourcesName);
				displayDepth.m_Texture = sceneDepth.m_Texture;
				displayDepth.m_DsvDesc = sceneDepth.m_DsvDesc;
				displayDepth.m_SrvDesc = sceneDepth.m_SrvDesc;
				displayDepth.m_Convention = sceneDepth.m_Convention;

				if (geometryMotion)
				{
					auto& temporalGeometry = blackboard.Get<RGTemporalGeometryResources>(
						TemporalGeometryResourcesName);
					const RHITextureDesc motionDesc = MakeTemporalMotionTextureDesc(width, height);
					temporalGeometry.m_MotionVectors =
						builder.CreateTexture("Temporal.MotionVectors", motionDesc);
					temporalGeometry.m_MotionSrvDesc =
						MakeRHITexture2DViewDesc(TemporalMotionFormat);
					temporalGeometry.m_Width = width;
					temporalGeometry.m_Height = height;
				}
			});

		if (context.GetTemporalFramePlan().HasService(TemporalService::GeometryMotion))
		{
			rg.AddPass<ClearMotionVectorsPassData>(
				"View.ClearMotionVectors",
				[](RenderGraph::RGBuilder& builder, ClearMotionVectorsPassData& data)
				{
					builder.SideEffect();
					auto& temporalGeometry = builder.GetBlackboard().Get<
						RGTemporalGeometryResources>(TemporalGeometryResourcesName);
					GGLAB_ASSERT_MSG(temporalGeometry.IsValid(),
						"Motion clear requires active temporal geometry resources.");
					builder.WriteInPlace(
						temporalGeometry.m_MotionVectors, RGTextureAccess::RenderTarget);
					data.m_Motion = temporalGeometry.m_MotionVectors;
					data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(
						data.m_Motion);
				},
				[](RGExecuteContext& executeContext, ClearMotionVectorsPassData& data)
				{
					auto* commandContext = executeContext.GetGraphicsCommandContext();
					GGLAB_ASSERT_NOT_NULL(commandContext);
					const RHIRenderingAttachment motionAttachment{
						.m_View = executeContext.GetViewHandle(data.m_Rtv),
						.m_LoadOp = RHIContentLoadOp::DontCare,
					};
					commandContext->BeginRendering({ .m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(&motionAttachment, 1) });
					commandContext->ClearColorAttachment(0, TemporalMotionClearColor);
				});
		}

		// Shadow Setup
		rg.AddPass<ShadowSetupPassData>("ShadowMap.Setup",
			[services, &context](RenderGraph::RGBuilder& builder, ShadowSetupPassData&)
			{
				const auto& shadowPlan = context.GetDirectionalShadowFramePlan();
				const auto& shadowSettings = shadowPlan.m_Settings;
				auto& shadowRes =
					builder.GetBlackboard().GetOrCreate<RGShadowResources>(ShadowResourcesName);
				if (shadowPlan.m_Cascades.empty())
				{
					return;
				}
				shadowRes.m_ShadowMapSize = shadowSettings.m_ShadowMapSize;

				shadowRes.m_CascadeCount = static_cast<uint32_t>(shadowPlan.m_Cascades.size());
				RHITextureDesc shadowMapDesc{};
				shadowMapDesc.m_ArraySize = static_cast<uint16_t>(shadowRes.m_CascadeCount);
				shadowMapDesc.m_Extent = { shadowRes.m_ShadowMapSize, shadowRes.m_ShadowMapSize, 1u };
				shadowMapDesc.m_Format = RHIFormat::R32Typeless;
				shadowRes.m_DirectionalShadowMap =
					builder.CreateTexture("Shadow.DirectionalShadowMap", shadowMapDesc);

				if (!shadowPlan.m_PreviewRequested)
				{
					return;
				}
				shadowRes.m_ShadowMapPreviewSize = DefaultDirectionalShadowMapPreviewSize;
				auto* renderResourceRegistry = services.m_Resources;
				GGLAB_ASSERT_NOT_NULL(renderResourceRegistry);
				renderResourceRegistry->EnsureShadowPreviewResources(
					shadowRes.m_ShadowMapPreviewSize);

				const auto* shadowMapPreviewDesc = renderResourceRegistry->GetTextureDesc(
					RenderTextureIndex::Preview_Shadow_DirectionalShadowMap);
				GGLAB_ASSERT_NOT_NULL(shadowMapPreviewDesc);
				const bool shadowPreviewInitialized = !renderResourceRegistry->IsDirty(
					RenderTextureIndex::Preview_Shadow_DirectionalShadowMap);
				const RGPersistentTextureImportContract shadowPreviewImport =
					ResolveRGPersistentTextureImportContract(shadowPreviewInitialized);

				shadowRes.m_DirectionalShadowMapPreview = builder.ImportTexture(
					"Shadow.DirectionalShadowMapPreview",
					renderResourceRegistry->GetTextureHandle(
						RenderTextureIndex::Preview_Shadow_DirectionalShadowMap),
					*shadowMapPreviewDesc, shadowPreviewImport.m_InitialState,
					shadowPreviewImport.m_InitialContentValidity);
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
			[services](RGExecuteContext& executeContext, PrepareBackBufferPassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				const RHIRenderingAttachment colorAttachment{
					.m_View = rtv,
					.m_LoadOp = RHIContentLoadOp::DontCare,
				};
				commandContext->BeginRendering({ .m_ColorAttachments =
					std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
				commandContext->ClearColorAttachment(0,
					services.m_Presentation->GetBackBufferClearColor());
			});

		// IBL Pass
		m_IBLPass.AddPass(rg, context, services);

		// Directional Shadow Map
		m_DirectionalShadowMapPass.AddPass(rg, context, services);

		// ShadowMap Preview
		if (context.GetDirectionalShadowFramePlan().m_PreviewRequested)
		{
			m_ShadowMapPreviewPass.AddPass(rg, context, services);
		}

		// Clear HDR color before background and scene geometry.
		m_ClearViewTargetsPass.AddPass(rg, context, services);

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
			GGLAB_ASSERT_MSG(forwardPlusActive,
				"Opaque Forward shading requires the Forward+ cull of the same frame.");
			m_ForwardOpaquePass.AddPass(rg, context, services);
			if (forwardPlusValidationEnabled)
			{
				m_ForwardPlusValidationPass.AddPass(rg, context, services);
			}
		}

		if (depthCoverageFramePlan.AddsForwardOpaquePass())
		{
			m_AerialPerspectivePass.AddPass(rg, context, services);
		}

		if (context.GetTemporalFramePlan().IsConsumerActive(TemporalConsumer::TemporalAA))
		{
			m_TemporalAAPass.AddPass(rg, context, services);
		}

		// Temporal boundary: post-temporal composition and post-processing read the
		// display-domain color from here on. An active resolve published its output;
		// otherwise the domains are equal and the composed scene color is the display color.
		{
			auto& targets = rg.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName)
				.GetViewTargets(displayViewId);
			if (!targets.m_DisplayColor.IsValid())
			{
				GGLAB_ASSERT_MSG(targets.m_RenderWidth == targets.m_DisplayWidth &&
					targets.m_RenderHeight == targets.m_DisplayHeight,
					"Without a temporal resolve the render and display extents are equal.");
				targets.m_DisplayColor = targets.m_SceneColor;
			}
		}

		// Scene extensions are post-TAA participants in the current temporal contract.
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

		// An evaluation reference sample accumulates the complete HDR scene, including
		// transparent and depth-tested debug geometry, before post-processing.
		m_TemporalReferencePass.AddPass(rg, context, services);
		m_PostProcessPipeline.AddPasses(rg, context, services);

		// The scene capture tap reads the post-processed display target before any
		// back-buffer preview or overlay composes into it.
		m_SceneCapturePass.AddPass(rg, context, services);
		// Diagnostic captures read a separate display-resolution tap visualization.
		m_PostProcessPipeline.AddDiagnosticCapturePass(rg, context, services);
		m_DiagnosticCapturePass.AddPass(rg, context, services);

		// IBL Preview
		m_IBLPreviewPass.AddPass(rg, context, services);

		// Always-visible debug geometry is composed after all back-buffer previews.
		m_DebugDrawOverlayPass.AddPass(rg, context, services);

		if (services.m_OverlayExtension)
		{
			services.m_OverlayExtension->AddOverlayPasses(rg, context, services);
		}

		// Return the persistent shadow preview to Common after DevelopGui and other
		// overlay consumers have declared their reads for this frame.
		if (context.GetDirectionalShadowFramePlan().m_PreviewRequested)
		{
			m_ShadowMapPreviewPass.AddFinishPass(rg);
		}

		// Return persistent IBL resources to Common only after every consumer and
		// preview pass has declared its final access for this frame.
		m_IBLPass.AddFinishPass(rg);

		// Atmosphere LUT previews must declare their reads before the persistent cache is exported.
		m_AtmospherePass.AddFinishPass(rg);

		// The composited capture tap reads the final display target after every
		// writer and before the present export.
		m_CompositedCapturePass.AddPass(rg, context, services);

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

	RenderFrameValidationResult ClassifyForwardPlusFrame(
		const ForwardPlusFrameValidationInputs& inputs) noexcept
	{
		if (!inputs.m_PresentationAvailable)
		{
			return RenderFrameValidationResult::ContractFailure(
				"The presentation swap chain is unavailable after the frame began.");
		}
		if (!inputs.m_DisplayExtentMatchesPresentation)
		{
			return RenderFrameValidationResult::Skip(
				"The display view was built for a different swap-chain extent.");
		}
		if (!inputs.m_RenderSceneReady)
		{
			return RenderFrameValidationResult::ContractFailure(
				"The frame requires prepared scene GPU data.");
		}
		if (inputs.m_HasOpaqueDraws &&
			!IsForwardPlusGlobalLightCountSupported(inputs.m_GlobalLightCount))
		{
			return RenderFrameValidationResult::ContractFailure(
				"The scene exceeds the Forward+ global-light capacity.",
				std::format("requested {}, limit {}", inputs.m_GlobalLightCount,
					ForwardPlusGlobalLightCapacity));
		}
		if (!inputs.m_DepthCoverageValid)
		{
			return RenderFrameValidationResult::ContractFailure(
				"The frame requires depth-prepass EQUAL coverage for every opaque draw.",
				std::string(inputs.m_DepthCoverageDiagnostic));
		}
		if (inputs.m_HasOpaqueDraws && inputs.m_GTAOEnabledAndSupported &&
			!inputs.m_GTAOPipelineAvailable)
		{
			return RenderFrameValidationResult::ContractFailure(
				"GTAO is enabled but its compute pipeline recipes failed to prepare.");
		}
		if (inputs.m_TemporalActive && !inputs.m_TemporalResolveClosureValid)
		{
			return RenderFrameValidationResult::ContractFailure(
				"An active temporal frame requires its resolve pipeline closure.");
		}
		return RenderFrameValidationResult::Ready();
	}

	RenderFrameValidationResult RenderPipelineForwardPlus::ValidateRenderFrame(
		const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		m_FramePlan.reset();
		const auto* swapChain = services.m_Presentation->GetSwapChain();
		if (!swapChain || !swapChain->IsValid())
		{
			return ClassifyForwardPlusFrame({ .m_PresentationAvailable = false });
		}
		const RenderView& displayView = context.GetDisplayRenderView();
		if (displayView.m_DisplayWidth != swapChain->GetBufferWidth() ||
			displayView.m_DisplayHeight != swapChain->GetBufferHeight())
		{
			return ClassifyForwardPlusFrame({ .m_PresentationAvailable = true });
		}

		PrepareForwardPasses(services, context.m_RenderScene.m_HasMaterialDiagnostics);
		m_GTAOPass.Prepare(services);
		const bool temporalActive =
			context.GetTemporalFramePlan().IsConsumerActive(TemporalConsumer::TemporalAA);
		bool temporalResolveClosureValid = false;
		if (temporalActive)
		{
			m_TemporalAAPass.Prepare(services);
			temporalResolveClosureValid = m_TemporalAAPass.ValidatePipelineClosure(services);
		}

		FramePlan plan{
			.m_FrameSerial = context.m_FrameSerial,
			.m_DepthCoverage = BuildDepthCoverageFramePlanForFrame(
				context, swapChain->GetBufferWidth(), swapChain->GetBufferHeight()),
		};
		const DepthCoverageFramePlan& depthCoverage = plan.m_DepthCoverage;
		plan.m_ForwardPlusStatus = depthCoverage.m_HasDepthCoverageDraws
			? ForwardPlusFrameStatus::Active : ForwardPlusFrameStatus::NoOpaqueDraws;
		const bool gtaoEnabled = context.GetDisplayViewRenderSettings().m_Lighting.m_GTAO.m_Enabled;
		const bool gtaoCoreAvailable = m_GTAOPass.GetCapabilityStatus().IsCoreAvailable();
		plan.m_GTAOStatus = ResolveGTAOFrameStatus(
			gtaoEnabled, gtaoCoreAvailable, depthCoverage.m_HasDepthCoverageDraws);

		RenderFrameValidationResult result = ClassifyForwardPlusFrame({
			.m_PresentationAvailable = true,
			.m_DisplayExtentMatchesPresentation = true,
			.m_RenderSceneReady = context.IsRenderSceneReady(),
			.m_HasOpaqueDraws = depthCoverage.m_HasDepthCoverageDraws,
			.m_GlobalLightCount =
				static_cast<uint32_t>(context.m_RenderScene.m_GlobalLightIndices.size()),
			.m_DepthCoverageValid = depthCoverage.IsValid(),
			.m_DepthCoverageDiagnostic = depthCoverage.m_Diagnostic,
			.m_GTAOEnabledAndSupported = gtaoEnabled && gtaoCoreAvailable,
			.m_GTAOPipelineAvailable = m_GTAOPass.IsAvailable(),
			.m_TemporalActive = temporalActive,
			.m_TemporalResolveClosureValid = temporalResolveClosureValid,
			});
		const TemporalFrameTransaction* transaction = context.m_TemporalFrameTransaction;
		if (result.IsReady() && transaction && transaction->GetReferenceSample())
		{
			m_TemporalReferencePass.Prepare(services);
			if (!transaction->CanAccumulateReference())
			{
				return RenderFrameValidationResult::ContractFailure(
					"Temporal reference sample cannot be accumulated",
					"The sum pair could not be allocated or the sample does not follow the "
					"committed sum.");
			}
			if (!m_TemporalReferencePass.ValidatePipelineClosure(services))
			{
				return RenderFrameValidationResult::ContractFailure(
					"Temporal reference accumulation pipeline unavailable");
			}
		}
		if (result.IsReady())
		{
			m_FramePlan = std::move(plan);
		}
		return result;
	}

	void RenderPipelineForwardPlus::PrepareForwardPasses(
		const RenderServices& services, bool materialDiagnostics) noexcept
	{
		auto* shaderManager = services.m_ShaderPrograms;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_ForwardPBRShaderSet.IsValid())
		{
			m_ForwardPBRShaderSet.m_CoverageVertexShader =
				shaderManager->LoadProgram(shader_programs::ForwardCoverageVertex);
			m_ForwardPBRShaderSet.m_AllLightsShadingPixelShader =
				shaderManager->LoadProgram(shader_programs::ForwardPBRAllLightsPixel);
			m_ForwardPBRShaderSet.m_ForwardPlus.m_Shading =
				shaderManager->LoadProgram(shader_programs::ForwardPBRForwardPlusPixel);
			m_ForwardPBRShaderSet.m_ForwardPlus.m_GTAOContribution =
				shaderManager->LoadProgram(shader_programs::ForwardPBRForwardPlusGTAOPixel);
			m_ForwardPBRShaderSet.m_AlphaTestPixelShader =
				shaderManager->LoadProgram(shader_programs::DepthPrepassAlphaTestPixel);
			m_ForwardPBRShaderSet.m_VelocityOpaquePixelShader =
				shaderManager->LoadProgram(shader_programs::DepthPrepassVelocityOpaquePixel);
			m_ForwardPBRShaderSet.m_VelocityAlphaTestPixelShader =
				shaderManager->LoadProgram(shader_programs::DepthPrepassVelocityAlphaTestPixel);
			if (m_ForwardPBRShaderSet.m_IncludesHdrDiffValidation)
			{
				m_ForwardPBRShaderSet.m_ForwardPlusValidation.m_Shading =
					shaderManager->LoadProgram(
						shader_programs::ForwardPBRForwardPlusValidationPixel);
				m_ForwardPBRShaderSet.m_ForwardPlusValidation.m_GTAOContribution =
					shaderManager->LoadProgram(
						shader_programs::ForwardPBRForwardPlusValidationGTAOPixel);
			}
		}

		if (!m_ForwardPBRShaderSet.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Forward renderer failed to prepare its required shared shader set.");
			GGLAB_UNREACHABLE("Forward renderer production shaders are unavailable.");
		}
		if (materialDiagnostics && !m_ForwardPBRShaderSet.AreMaterialDiagnosticsValid())
		{
			m_ForwardPBRShaderSet.m_AllLightsMaterialDiagnosticsPixelShader =
				shaderManager->LoadProgram(shader_programs::ForwardPBRAllLightsMaterialDiagnosticsPixel);
			m_ForwardPBRShaderSet.m_ForwardPlus.m_MaterialDiagnostics =
				shaderManager->LoadProgram(shader_programs::ForwardPBRForwardPlusMaterialDiagnosticsPixel);
			m_ForwardPBRShaderSet.m_ForwardPlus.m_GTAOContributionMaterialDiagnostics =
				shaderManager->LoadProgram(shader_programs::ForwardPBRForwardPlusGTAOMaterialDiagnosticsPixel);
			if (m_ForwardPBRShaderSet.m_IncludesHdrDiffValidation)
			{
				auto& validation = m_ForwardPBRShaderSet.m_ForwardPlusValidation;
				validation.m_MaterialDiagnostics = shaderManager->LoadProgram(
					shader_programs::ForwardPBRForwardPlusValidationMaterialDiagnosticsPixel);
				validation.m_GTAOContributionMaterialDiagnostics = shaderManager->LoadProgram(
					shader_programs::ForwardPBRForwardPlusValidationGTAOMaterialDiagnosticsPixel);
			}
			GGLAB_ASSERT_MSG(m_ForwardPBRShaderSet.AreMaterialDiagnosticsValid(),
				"Material diagnostic output requires every composed Forward shader variant.");
		}
		m_DepthPrepassPass.Prepare(services, m_ForwardPBRShaderSet);
		m_ForwardOpaquePass.Prepare(services, m_ForwardPBRShaderSet);
		m_ForwardTransparentPass.Prepare(services, m_ForwardPBRShaderSet);
	}

	DepthCoverageFramePlan RenderPipelineForwardPlus::BuildDepthCoverageFramePlanForFrame(
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
