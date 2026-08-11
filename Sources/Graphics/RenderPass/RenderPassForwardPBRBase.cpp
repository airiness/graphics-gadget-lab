#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassForwardPBRBase.h"
#include "Graphics/Pipeline/ForwardPlus.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RenderPass/ForwardPlusGraphResources.h"
#include "Graphics/RenderPass/ForwardPlusValidationGraphResources.h"
#include "Graphics/RenderPass/GTAOGraphResources.h"
#include "Graphics/RenderPass/IBLGraphResources.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPass/ShadowGraphResources.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHIContext.h"
#include "Graphics/RHI/RHIPipelineSystem.h"
#include "Graphics/RHI/RHITextureViewDescUtils.h"
#include "Graphics/SamplerRegistry.h"

namespace gglab
{
	namespace
	{
		enum class ForwardPBRRootParameter : uint32_t
		{
			TileHeaders = static_cast<uint32_t>(CommonRSRootParamIndex::Count),
			TileIndices,
		};

		struct ForwardPBRPassParameters
		{
			uint32_t m_ViewIndex = 0;
			uint32_t m_ShadowMapTextureIndex = 0;
			uint32_t m_ShadowMapSamplerIndex = 0;
			uint32_t m_ShadowMapSize = 0;
			uint32_t m_ShadowFlags = 0;
			float m_ShadowReceiverDepthBias = 0.0f;
			uint32_t m_ShadowViewIndex = 0;
			uint32_t m_ForwardPlusTileCountX = 0;
			uint32_t m_ForwardPlusTileCountY = 0;
			uint32_t m_ForwardPlusGlobalLightCount = 0;
			std::array<uint32_t, 2> m_ForwardPlusGlobalLightIndices01{};
			std::array<uint32_t, 2> m_ForwardPlusGlobalLightIndices23{};
			uint32_t m_GTAOTextureIndex = 0;
			uint32_t m_GTAOFlags = 0;
		};
		static_assert(IsPassRootConstantStruct<ForwardPBRPassParameters>);
		static_assert(sizeof(ForwardPBRPassParameters) == 64);

		struct PassData
		{
			RGTextureId m_SceneColor{};
			RGTextureId m_Depth{};
			RGTextureId m_IrradianceCubemap{};
			RGTextureId m_PrefilteredSpecularCubemap{};
			RGTextureId m_BrdfLut{};
			RGTextureId m_ShadowMap{};
			RGTextureId m_GTAOFinalAO{};
			RGTextureId m_GTAOContribution{};
			RGTextureId m_LegacyReferenceColor{};
			RGBufferId m_TileHeaders{};
			RGBufferId m_TileIndices{};

			RGTextureViewId m_Rtv{};
			RGTextureViewId m_Dsv{};
			RGTextureViewId m_ShadowSrv{};
			RGTextureViewId m_GTAOFinalAOSrv{};
			RGTextureViewId m_GTAOContributionRtv{};
			RGTextureViewId m_LegacyReferenceRtv{};

			const DepthCoverageRasterDomain* m_RasterDomain = nullptr;
			const RenderQueue* m_ExpectedRenderQueue = nullptr;
			ForwardPlusTileGrid m_ForwardPlusTileGrid{};
			ForwardPBRLightingVariant m_LightingVariant = ForwardPBRLightingVariant::Legacy;
			bool m_UseDepthEqual = false;
			bool m_ClearDepth = false;
			float m_ClearDepthValue = 0.0f;
			uint32_t m_ShadowMapSize = 0;
			uint32_t m_ShadowSamplerIndex = 0;
			uint32_t m_ShadowFlags = 0;
			float m_ShadowReceiverDepthBias = 0.0f;
			bool m_GTAOEnabled = false;
			bool m_GTAOContributionOutputEnabled = false;
		};

		inline constexpr uint32_t GTAOEnabledFlag = 1u;

		void AppendForwardPBRBindingSlot(RHIBindingLayoutDesc& desc, uint32_t binding,
			const char* debugName) noexcept
		{
			GGLAB_ASSERT(desc.m_SlotCount < desc.MaxSlots);
			desc.m_Slots[desc.m_SlotCount++] = {
				.m_Type = RHIBindingType::ReadOnlyStorageBuffer,
				.m_Visibility = RHIShaderStage::Pixel,
				.m_Binding = binding,
				.m_Space = 0,
				.m_Count = 1,
				.m_DebugName = debugName,
			};
		}

		RHIBindingLayoutDesc BuildForwardPBRBindingLayout() noexcept
		{
			RHIBindingLayoutDesc desc = Renderer::BuildCommonRHIBindingLayoutDesc();
			desc.m_DebugName = "ForwardPBR.ForwardPlusBindingLayout";
			AppendForwardPBRBindingSlot(desc, 5, "ForwardPlusTileHeaders");
			AppendForwardPBRBindingSlot(desc, 6, "ForwardPlusTileIndices");
			return desc;
		}
	}

	void RenderPassForwardPBRBase::AddForwardPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_IsInitialized, "Forward pass must be prepared before graph construction.");
		auto* contextPtr = &context;
		GGLAB_ASSERT_NOT_NULL(contextPtr);

		auto* servicesPtr = &services;
		GGLAB_ASSERT_NOT_NULL(servicesPtr);

		const RenderViewID displayViewId = context.GetDisplayViewId();
		const bool transparent = m_PassKind == ForwardPBRPassKind::Transparent;
		const ForwardPBRLightingVariant lightingVariant = ResolveForwardPBRLightingVariant(
			m_PassKind, context.GetDisplayViewRenderSettings().m_Lighting.m_ForwardPlus,
			m_HdrDiffValidationAvailable);
		auto* registry = services.m_Renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(registry);
		const bool gtaoContributionRequested = !transparent &&
			registry->IsPostProcessPreviewRequested() &&
			registry->GetPostProcessPreviewSelection().m_Tap ==
				PostProcessDebugTap::GTAOAOOnlyLightingContribution;

		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[contextPtr, servicesPtr, displayViewId, transparent, lightingVariant,
			gtaoContributionRequested](
				RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();

				auto& targetsTable =
					blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& displayTargets = targetsTable.GetViewTargets(displayViewId);
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);
				auto& shadowRes = blackboard.Get<RGShadowResources>(ShadowResourcesName);
				auto& sceneDepth = blackboard.Get<RGSceneDepthResources>(SceneDepthResourcesName);
				const auto& framePlan =
					blackboard.Get<DepthCoverageFramePlan>(DepthCoverageFramePlanName);
				const auto& renderQueue = contextPtr->GetRenderQueue(displayViewId);

				builder.ReadWriteInPlace(
					displayTargets.m_SceneColor, RGTextureAccess::RenderTarget);
				data.m_SceneColor = displayTargets.m_SceneColor;
				data.m_IrradianceCubemap =
					builder.Read(iblRes.m_IrradianceCubemap, RGTextureAccess::Sample);
				data.m_PrefilteredSpecularCubemap =
					builder.Read(iblRes.m_PrefilteredSpecularCubemap, RGTextureAccess::Sample);
				data.m_BrdfLut = builder.Read(iblRes.m_BrdfLut, RGTextureAccess::Sample);
				data.m_ShadowMap =
					builder.Read(shadowRes.m_DirectionalShadowMap, RGTextureAccess::Sample);
				const auto* gtao = blackboard.TryGet<RGGTAOResources>(GTAOResourcesName);
				if (!transparent && gtao && gtao->IsComplete())
				{
					const RHITextureDesc& gtaoDesc = builder.GetTextureDesc(gtao->m_FinalAO);
					const RHITextureDesc& sceneColorDesc =
						builder.GetTextureDesc(displayTargets.m_SceneColor);
					GGLAB_ASSERT_MSG(gtaoDesc.m_Dimension == RHITextureDimension::Texture2D &&
						(gtaoDesc.m_Format == RHIFormat::R8Unorm ||
							gtaoDesc.m_Format == RHIFormat::R16Float) &&
						gtaoDesc.m_Extent.m_Width == sceneColorDesc.m_Extent.m_Width &&
						gtaoDesc.m_Extent.m_Height == sceneColorDesc.m_Extent.m_Height,
						"GTAO FinalAO must use its resolved single-channel format and match the opaque "
						"display target extent.");
					data.m_GTAOFinalAO = builder.Read(
						gtao->m_FinalAO, RGTextureAccess::Sample, RHIStage::PixelShader);
					data.m_GTAOFinalAOSrv =
						builder.CreateView<RHITextureViewType::ShaderResource>(data.m_GTAOFinalAO);
					data.m_GTAOEnabled = true;
					if (gtaoContributionRequested)
					{
						RHITextureDesc contributionDesc = sceneColorDesc;
						contributionDesc.m_Format = RHIFormat::R16G16B16A16Float;
						auto& mutableGtao = blackboard.Get<RGGTAOResources>(GTAOResourcesName);
						mutableGtao.m_AOOnlyLightingContribution = builder.CreateTexture(
							"GTAO.AOOnlyLightingContribution", contributionDesc);
						builder.WriteInPlace(mutableGtao.m_AOOnlyLightingContribution,
							RGTextureAccess::RenderTarget);
						data.m_GTAOContribution = mutableGtao.m_AOOnlyLightingContribution;
						data.m_GTAOContributionRtv =
							builder.CreateView<RHITextureViewType::RenderTarget>(
								data.m_GTAOContribution);
						data.m_GTAOContributionOutputEnabled = true;
					}
				}

				data.m_Rtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_SceneColor);
				data.m_LightingVariant = lightingVariant;
				const auto* forwardPlus = blackboard.TryGet<RGForwardPlusResources>(
					ForwardPlusResourcesName);
				if (data.m_LightingVariant != ForwardPBRLightingVariant::Legacy &&
					(!forwardPlus || !forwardPlus->IsValid()))
				{
					data.m_LightingVariant = ForwardPBRLightingVariant::Legacy;
				}

				if (data.m_LightingVariant != ForwardPBRLightingVariant::Legacy)
				{
					data.m_ForwardPlusTileGrid = forwardPlus->m_TileGrid;
					data.m_TileHeaders = builder.Read(forwardPlus->m_TileLightHeaders,
						RGBufferAccess::StructuredRead, RHIStage::PixelShader);
					data.m_TileIndices = builder.Read(forwardPlus->m_TileLightIndices,
						RGBufferAccess::StructuredRead, RHIStage::PixelShader);
				}

				if (data.m_LightingVariant == ForwardPBRLightingVariant::ForwardPlusValidation)
				{
					RHITextureDesc referenceDesc = builder.GetTextureDesc(data.m_SceneColor);
					auto& validation = blackboard.Create<RGForwardPlusValidationResources>(
						ForwardPlusValidationResourcesName);
					validation.m_LegacyReferenceColor = builder.CreateTexture(
						"ForwardPlus.LegacyReferenceColor", referenceDesc);
					builder.WriteInPlace(
						validation.m_LegacyReferenceColor, RGTextureAccess::RenderTarget);
					data.m_LegacyReferenceColor = validation.m_LegacyReferenceColor;
					data.m_LegacyReferenceRtv =
						builder.CreateView<RHITextureViewType::RenderTarget>(
							data.m_LegacyReferenceColor);
				}

				const auto shadowSrvDesc =
					MakeRHITexture2DViewDesc(RHIFormat::R32Float, 0, 1, RHITextureAspect::Depth);
				data.m_ShadowSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
					data.m_ShadowMap, shadowSrvDesc);

				data.m_RasterDomain = std::addressof(renderQueue.m_CoverageRasterDomain);
				data.m_ExpectedRenderQueue = framePlan.m_SourceRenderQueue;
				data.m_ShadowMapSize = shadowRes.m_ShadowMapSize;

				if (!renderQueue.m_DrawItems.empty())
				{
					GGLAB_ASSERT_MSG(data.m_RasterDomain->IsValid(),
						"Forward rendering requires a valid coverage raster domain.");
					GGLAB_ASSERT_MSG(
						data.m_RasterDomain->m_DepthConvention == sceneDepth.m_Convention,
						"Forward raster and resource depth conventions must match.");
					GGLAB_ASSERT_MSG(AreDepthCoverageTargetExtentsCompatible(*data.m_RasterDomain,
						builder.GetTextureDesc(displayTargets.m_SceneColor),
						builder.GetTextureDesc(sceneDepth.m_Texture)),
						"Forward color and depth extents must match the coverage raster domain.");
					GGLAB_ASSERT_MSG(framePlan.m_RasterDomain == data.m_RasterDomain,
						"Forward must consume the frame-plan raster domain.");
					GGLAB_ASSERT_MSG(data.m_ExpectedRenderQueue == std::addressof(renderQueue),
						"Forward must consume the frame-plan RenderQueue and its shared draw packets.");
				}

				if (!transparent)
				{
					data.m_UseDepthEqual = framePlan.UsesDepthPrepassEqual();
					data.m_ClearDepth = framePlan.UsesForwardDepthWrite();
					data.m_ClearDepthValue =
						screen_space::GetDepthBackgroundValue(sceneDepth.m_Convention);
					GGLAB_ASSERT_MSG(framePlan.RendersGeometry(),
						"Opaque Forward pass must not be added for a rejected geometry frame.");
				}

				if (transparent || data.m_UseDepthEqual)
				{
					data.m_Depth =
						builder.Read(sceneDepth.m_Texture, RGTextureAccess::DepthStencilRead);
					RHITextureViewDesc readOnlyDsvDesc = sceneDepth.m_DsvDesc;
					readOnlyDsvDesc.m_ReadOnlyDepth = true;
					data.m_Dsv = builder.CreateView<RHITextureViewType::DepthStencil>(
						data.m_Depth, readOnlyDsvDesc);
				}
				else
				{
					builder.ReadWriteInPlace(
						sceneDepth.m_Texture, RGTextureAccess::DepthStencilWrite);
					data.m_Depth = sceneDepth.m_Texture;
					data.m_Dsv = builder.CreateView<RHITextureViewType::DepthStencil>(
						data.m_Depth, sceneDepth.m_DsvDesc);
				}

				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);
				data.m_ShadowSamplerIndex = renderer->GetSamplerRegistry()->GetSamplerIndex(
					SamplerPreset::ShadowCmpLinearClamp);

				const auto& shadowSettings = contextPtr->GetDirectionalShadowSettings();
				data.m_ShadowFlags =
					(shadowSettings.m_Enable ? 1u : 0u) | (shadowSettings.m_EnablePCF ? 2u : 0u);
				data.m_ShadowReceiverDepthBias = shadowSettings.m_ReceiverDepthBias;
			},
			[this, contextPtr, servicesPtr, displayViewId](
				RGExecuteContext& executeContext, PassData& data)
			{
				auto* graphicsContext = executeContext.GetGraphicsCommandContext();
				GGLAB_ASSERT_NOT_NULL(graphicsContext);

				const RHITextureViewHandle rtv = executeContext.GetViewHandle(data.m_Rtv);
				const auto dsv = executeContext.GetViewHandle(data.m_Dsv);
				std::array<RHIRenderingAttachment, 3> renderTargets{
					RHIRenderingAttachment{ .m_View = rtv }, {}, {}
				};
				uint32_t renderTargetCount = 1;
				if (data.m_LightingVariant == ForwardPBRLightingVariant::ForwardPlusValidation)
				{
					renderTargets[1] = {
						.m_View = executeContext.GetViewHandle(data.m_LegacyReferenceRtv),
						.m_LoadOp = RHIContentLoadOp::DontCare,
					};
					GGLAB_ASSERT_MSG(renderTargets[1].m_View.IsValid(),
						"Forward+ HDR diff requires a legacy reference render target.");
					renderTargetCount = 2;
				}
				if (data.m_GTAOContributionOutputEnabled)
				{
					renderTargets[renderTargetCount] = {
						.m_View = executeContext.GetViewHandle(data.m_GTAOContributionRtv),
						.m_LoadOp = RHIContentLoadOp::DontCare,
					};
					GGLAB_ASSERT_MSG(renderTargets[renderTargetCount].m_View.IsValid(),
						"GTAO contribution preview requires a render-target view.");
					++renderTargetCount;
				}
				graphicsContext->BeginRendering({
					.m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(renderTargets.data(), renderTargetCount),
					.m_DepthAttachment = RHIRenderingAttachment{ .m_View = dsv },
				});
				if (data.m_LightingVariant == ForwardPBRLightingVariant::ForwardPlusValidation)
				{
					graphicsContext->ClearColorAttachment(1, { 0.0f, 0.0f, 0.0f, 1.0f });
				}
				if (data.m_GTAOContributionOutputEnabled)
				{
					graphicsContext->ClearColorAttachment(
						renderTargetCount - 1, { 0.0f, 0.0f, 0.0f, 1.0f });
				}
				if (data.m_ClearDepth)
				{
					graphicsContext->ClearDepthAttachment(data.m_ClearDepthValue);
				}

				const auto shadowSrv = executeContext.GetViewDescriptor(data.m_ShadowSrv);
				GGLAB_ASSERT_MSG(shadowSrv.IsValid(),
					"ForwardPBR shadow map SRV must expose a descriptor heap index.");

				auto* renderer = servicesPtr->m_Renderer;
				uint32_t gtaoTextureIndex = 0;
				if (data.m_GTAOEnabled)
				{
					const auto gtaoSrv = executeContext.GetViewDescriptor(data.m_GTAOFinalAOSrv);
					GGLAB_ASSERT_MSG(gtaoSrv.IsValid(),
						"GTAO FinalAO must expose a shader-resource descriptor before opaque shading.");
					gtaoTextureIndex = gtaoSrv.m_Index;
				}
				const auto& globalLightIndices = contextPtr->m_RenderScene.m_GlobalLightIndices;
				std::array<uint32_t, ForwardPlusGlobalLightCapacity> packedGlobalLightIndices{};
				if (data.m_LightingVariant != ForwardPBRLightingVariant::Legacy)
				{
					GGLAB_ASSERT_MSG(IsForwardPlusGlobalLightCountSupported(
						static_cast<uint32_t>(globalLightIndices.size())),
						"Forward+ opaque shading requires a bounded global-light list.");
					std::ranges::copy(globalLightIndices, packedGlobalLightIndices.begin());
				}
				const auto& renderQueue = contextPtr->GetRenderQueue(displayViewId);
				if (renderQueue.m_DrawItems.empty())
				{
					return;
				}
				const auto& ranges = renderQueue.m_BucketDrawRanges;
				const DrawItemsRange* firstDrawRange = nullptr;
				if (m_PassKind == ForwardPBRPassKind::Transparent)
				{
					const auto& range = ranges[utils::ToIndex(RenderBucket::Transparent)];
					if (range.m_Count > 0)
					{
						firstDrawRange = std::addressof(range);
					}
				}
				else
				{
					for (const RenderBucket bucket :
					{RenderBucket::Opaque, RenderBucket::AlphaTest})
					{
						const auto& range = ranges[utils::ToIndex(bucket)];
						if (range.m_Count > 0)
						{
							firstDrawRange = std::addressof(range);
							break;
						}
					}
				}
				if (!firstDrawRange)
				{
					return;
				}
				GGLAB_ASSERT_MSG(firstDrawRange->m_Start < renderQueue.m_DrawItems.size(),
					"Forward first draw must be inside the frame-plan RenderQueue.");
				if (firstDrawRange->m_Start >= renderQueue.m_DrawItems.size())
				{
					return;
				}
				graphicsContext->SetPipeline(GetOrCreatePSOForVariant(*renderer,
					renderQueue.m_DrawItems[firstDrawRange->m_Start].m_VariantBits,
					data.m_UseDepthEqual, data.m_LightingVariant,
					data.m_GTAOContributionOutputEnabled));

				GGLAB_ASSERT_NOT_NULL(data.m_RasterDomain);
				GGLAB_ASSERT_MSG(
					data.m_RasterDomain == std::addressof(renderQueue.m_CoverageRasterDomain),
					"Forward must consume the RenderQueue raster domain directly.");
				graphicsContext->SetViewport(data.m_RasterDomain->m_Viewport);
				graphicsContext->SetScissorRect(data.m_RasterDomain->m_Scissor);
				graphicsContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

				const auto* sceneBuffer = renderer->GetSceneConstantBuffer();
				graphicsContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					sceneBuffer->GetBufferHandle(),
					contextPtr->m_RenderScene.m_SceneConstantBufferOffset);

				// Set object structured buffer
				const auto& objectSB = renderer->GetObjectStructuredBuffer();
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ObjectSB),
					objectSB->GetBufferHandle(contextPtr->m_FrameSlotIndex));

				// Set material structured buffer
				const auto& materialSB = renderer->GetMaterialStructuredBuffer();
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB),
					materialSB->GetBufferHandle(contextPtr->m_FrameSlotIndex));

				// View structured buffer
				const auto& viewSB = renderer->GetViewStructuredBuffer();
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					viewSB->GetBufferHandle());

				// Light structured buffer
				const auto& lightSB = renderer->GetLightStructuredBuffer();
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::LightSB),
					lightSB->GetBufferHandle(contextPtr->m_FrameSlotIndex));

				if (data.m_LightingVariant != ForwardPBRLightingVariant::Legacy)
				{
					const RHIBufferHandle tileHeaders =
						executeContext.GetBufferHandle(data.m_TileHeaders);
					const RHIBufferHandle tileIndices =
						executeContext.GetBufferHandle(data.m_TileIndices);
					GGLAB_ASSERT_MSG(tileHeaders.IsValid() && tileIndices.IsValid(),
						"Forward+ tile buffers must resolve before opaque shading.");
					graphicsContext->SetReadOnlyBuffer(
						static_cast<uint32_t>(ForwardPBRRootParameter::TileHeaders), tileHeaders);
					graphicsContext->SetReadOnlyBuffer(
						static_cast<uint32_t>(ForwardPBRRootParameter::TileIndices), tileIndices);
				}

				const ForwardPBRPassParameters passParameters{
					.m_ViewIndex = static_cast<uint32_t>(utils::ToIndex(displayViewId)),
					.m_ShadowMapTextureIndex = shadowSrv.m_Index,
					.m_ShadowMapSamplerIndex = data.m_ShadowSamplerIndex,
					.m_ShadowMapSize = data.m_ShadowMapSize,
					.m_ShadowFlags = data.m_ShadowFlags,
					.m_ShadowReceiverDepthBias = data.m_ShadowReceiverDepthBias,
					.m_ShadowViewIndex =
						static_cast<uint32_t>(utils::ToIndex(RenderViewID::DirectionalShadow)),
					.m_ForwardPlusTileCountX = data.m_ForwardPlusTileGrid.m_TileCountX,
					.m_ForwardPlusTileCountY = data.m_ForwardPlusTileGrid.m_TileCountY,
					.m_ForwardPlusGlobalLightCount = data.m_LightingVariant ==
						ForwardPBRLightingVariant::Legacy
						? 0u
						: static_cast<uint32_t>(globalLightIndices.size()),
					.m_ForwardPlusGlobalLightIndices01 = {
						packedGlobalLightIndices[0], packedGlobalLightIndices[1] },
					.m_ForwardPlusGlobalLightIndices23 = {
						packedGlobalLightIndices[2], packedGlobalLightIndices[3] },
					.m_GTAOTextureIndex = gtaoTextureIndex,
					.m_GTAOFlags = data.m_GTAOEnabled ? GTAOEnabledFlag : 0u,
				};
				graphicsContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), passParameters);

				DrawRenderQueue(graphicsContext, *contextPtr, *servicesPtr, displayViewId,
					data.m_ExpectedRenderQueue, data.m_UseDepthEqual, data.m_LightingVariant,
					data.m_GTAOContributionOutputEnabled);
			});
	}

	void RenderPassForwardPBRBase::Prepare(
		const RenderServices& services, const ForwardPBRShaderSet& shaderSet) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		if (!m_IsInitialized)
		{
			GGLAB_ASSERT_MSG(
				shaderSet.IsValid(), "Forward pass requires the shared Forward shader set.");
			if (!shaderSet.IsValid())
			{
				return;
			}

			// Pipeline recipe
			auto& legacyKey =
				m_BasePhysicalKeys[static_cast<size_t>(ForwardPBRLightingVariant::Legacy)][0];
			legacyKey.m_BindingLayout = renderer->GetCommonBindingLayout();
			legacyKey.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
			legacyKey.m_VSId = shaderSet.m_CoverageVertexShader;
			legacyKey.m_PSId = shaderSet.m_LegacyShadingPixelShader;

			legacyKey.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
			legacyKey.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
			legacyKey.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R16G16B16A16Float;
			legacyKey.m_Formats.m_RenderTargetCount = 1;
			legacyKey.m_Formats.m_DepthStencilFormat = RHIFormat::D32Float;
			legacyKey.m_Formats.m_SampleCount = 1;
			legacyKey.m_Formats.m_SampleQuality = 0;
			legacyKey.m_RasterizerPreset = RasterizerPreset::Default;
			legacyKey.m_BlendPreset = BlendPreset::Default;
			legacyKey.m_DepthPreset = DepthPreset::ReversedZWrite;

			if (m_PassKind == ForwardPBRPassKind::Opaque)
			{
				auto* rhiContext = renderer->GetRHIContext();
				GGLAB_ASSERT_NOT_NULL(rhiContext);
				const RHIBindingLayoutHandle forwardPlusBindingLayout =
					rhiContext->GetPipelineSystem().CreateBindingLayout(
						BuildForwardPBRBindingLayout());
				GGLAB_ASSERT_MSG(forwardPlusBindingLayout.IsValid(),
					"Forward+ opaque shading requires its pass-specific binding layout.");

				auto& forwardPlusKey = m_BasePhysicalKeys[
					static_cast<size_t>(ForwardPBRLightingVariant::ForwardPlus)][0];
				forwardPlusKey = legacyKey;
				forwardPlusKey.m_BindingLayout = forwardPlusBindingLayout;
				forwardPlusKey.m_PSId = shaderSet.m_ForwardPlusShadingPixelShader;

				auto& validationKey = m_BasePhysicalKeys[
					static_cast<size_t>(ForwardPBRLightingVariant::ForwardPlusValidation)][0];
				validationKey = forwardPlusKey;
				validationKey.m_PSId = shaderSet.m_ForwardPlusValidationPixelShader;
				validationKey.m_Formats.m_RenderTargetFormats[1] =
					RHIFormat::R16G16B16A16Float;
				validationKey.m_Formats.m_RenderTargetCount = 2;

				const ShaderID contributionShaders[] = {
					shaderSet.m_LegacyGTAOContributionPixelShader,
					shaderSet.m_ForwardPlusGTAOContributionPixelShader,
					shaderSet.m_ForwardPlusValidationGTAOContributionPixelShader,
				};
				for (size_t lightingIndex = 0; lightingIndex < LightingVariantCount; ++lightingIndex)
				{
					auto& contributionKey = m_BasePhysicalKeys[lightingIndex][1];
					contributionKey = m_BasePhysicalKeys[lightingIndex][0];
					contributionKey.m_PSId = contributionShaders[lightingIndex];
					const uint32_t targetIndex =
						contributionKey.m_Formats.m_RenderTargetCount;
					contributionKey.m_Formats.m_RenderTargetFormats[targetIndex] =
						RHIFormat::R16G16B16A16Float;
					++contributionKey.m_Formats.m_RenderTargetCount;
				}
			}

			m_IsInitialized = true;
		}
	}

	void RenderPassForwardPBRBase::DrawRenderQueue(RHIGraphicsCommandContext* graphicsContext,
		const RenderFrameContext& context, const RenderServices& services, RenderViewID viewId,
		const RenderQueue* expectedRenderQueue, bool useDepthEqual,
		ForwardPBRLightingVariant lightingVariant, bool gtaoContributionOutputEnabled) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(graphicsContext);
		const auto& renderQueue = context.GetRenderQueue(viewId);
		if (renderQueue.m_DrawItems.empty())
		{
			return;
		}
		GGLAB_ASSERT_MSG(renderQueue.m_CoverageRasterDomain.IsValid(),
			"Forward depth coverage requires one valid raster domain per view.");
		const auto ranges = renderQueue.m_BucketDrawRanges;

		if (m_PassKind == ForwardPBRPassKind::Transparent)
		{
			DrawRange(graphicsContext, services, renderQueue,
				ranges[utils::ToIndex(RenderBucket::Transparent)], false, expectedRenderQueue,
				lightingVariant, gtaoContributionOutputEnabled);
			return;
		}

		for (const RenderBucket bucket : {RenderBucket::Opaque, RenderBucket::AlphaTest})
		{
			const DrawItemsRange range = ranges[utils::ToIndex(bucket)];
			if (range.m_Count == 0)
			{
				continue;
			}
			DrawRange(
				graphicsContext, services, renderQueue, range, useDepthEqual, expectedRenderQueue,
				lightingVariant, gtaoContributionOutputEnabled);
		}
	}

	void RenderPassForwardPBRBase::DrawRange(RHIGraphicsCommandContext* graphicsContext,
		const RenderServices& services, const RenderQueue& renderQueue, const DrawItemsRange& range,
		bool useDepthEqual, const RenderQueue* expectedRenderQueue,
		ForwardPBRLightingVariant lightingVariant, bool gtaoContributionOutputEnabled) noexcept
	{
		if (range.m_Count == 0)
		{
			return;
		}
		GGLAB_ASSERT_NOT_NULL(graphicsContext);

		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		const auto& drawItems = renderQueue.m_DrawItems;

		uint64_t lastVariantBits = std::numeric_limits<uint64_t>::max();
		MeshID lastMeshId{};
		bool hasBoundMesh = false;

		for (uint32_t index = 0; index < range.m_Count; ++index)
		{
			const auto& drawItem = drawItems[range.m_Start + index];
			const DepthCoverageDrawPacket& drawPacket = drawItem.m_CoverageDrawPacket;
			GGLAB_ASSERT_MSG(drawPacket.IsValid(),
				"Forward received an incomplete shared depth coverage draw packet.");
			GGLAB_ASSERT_MSG(expectedRenderQueue == std::addressof(renderQueue) &&
				IsSameDepthCoverageDrawPacket(drawPacket,
					expectedRenderQueue->m_DrawItems[range.m_Start + index]
					.m_CoverageDrawPacket),
				"Forward must consume the exact draw-packet instances accepted by the frame plan.");

			// Set PSO
			if (drawItem.m_VariantBits != lastVariantBits)
			{
				graphicsContext->SetPipeline(GetOrCreatePSOForVariant(
					*services.m_Renderer, drawItem.m_VariantBits, useDepthEqual, lightingVariant,
					gtaoContributionOutputEnabled));

				lastVariantBits = drawItem.m_VariantBits;
			}

			const auto& geometry = drawPacket.m_Geometry;
			if (!hasBoundMesh || geometry.m_MeshId != lastMeshId)
			{
				graphicsContext->SetVertexBuffers(
					0, std::span<const RHIVertexBufferBinding>(&geometry.m_VertexBuffer, 1));
				graphicsContext->SetIndexBuffer(geometry.m_IndexBuffer);
				lastMeshId = geometry.m_MeshId;
				hasBoundMesh = true;
			}

			graphicsContext->SetPushConstants(
				static_cast<uint32_t>(CommonRSRootParamIndex::DrawConstants),
				drawPacket.m_DrawParameters);

			const auto& draw = drawPacket.m_IndexedDraw;
			graphicsContext->DrawIndexed(draw.m_IndexCount, draw.m_InstanceCount,
				draw.m_StartIndexLocation, draw.m_BaseVertexLocation, draw.m_StartInstanceLocation);
		}
	}

	RHIPipelineHandle RenderPassForwardPBRBase::GetOrCreatePSOForVariant(
		const Renderer& renderer, uint64_t variantBits, bool useDepthEqual,
		ForwardPBRLightingVariant lightingVariant, bool gtaoContributionOutputEnabled) noexcept
	{
		GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);

		const size_t lightingVariantIndex = static_cast<size_t>(lightingVariant);
		GGLAB_ASSERT(lightingVariantIndex < LightingVariantCount);
		const size_t contributionVariantIndex = gtaoContributionOutputEnabled ? 1u : 0u;
		GraphicsPipelineDescription description{
			.m_PhysicalKey = m_BasePhysicalKeys[lightingVariantIndex][contributionVariantIndex],
			.m_LogicalMetadata = BuildLogicalPipelineMetadataForVariant(
				m_BasePhysicalKeys[lightingVariantIndex][contributionVariantIndex], variantBits),
		};
		auto [rasterizerPreset, depthPreset, blendPreset] =
			GetPresetsFromVariantBits(variantBits, useDepthEqual);
		description.m_PhysicalKey.m_RasterizerPreset = rasterizerPreset;
		description.m_PhysicalKey.m_DepthPreset = depthPreset;
		description.m_PhysicalKey.m_BlendPreset = blendPreset;

		const size_t slotIndex = static_cast<size_t>(variantBits & RenderQueueBuilder::VariantMask);
		auto& slot =
			m_PipelineSlots[lightingVariantIndex][contributionVariantIndex][slotIndex];
		return pipelineCache->Resolve(slot, description.m_PhysicalKey, GetInfo());
	}

	std::optional<DepthCoveragePipelineSignature> RenderPassForwardPBRBase::
		BuildDepthCoveragePipelineSignatureForVariant(
			const GraphicsPhysicalPipelineKey& physicalKey, uint64_t variantBits) noexcept
	{
		GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);
		const RenderBucket bucket = RenderQueueBuilder::DecodeVariantBucket(variantBits);
		if (bucket == RenderBucket::Transparent)
		{
			return std::nullopt;
		}

		const bool doubleSided = RenderQueueBuilder::DecodeVariantDoubleSided(variantBits);
		GraphicsPhysicalPipelineKey coverageKey = physicalKey;
		coverageKey.m_RasterizerPreset =
			doubleSided ? RasterizerPreset::TwoSided : RasterizerPreset::Default;
		const DepthCoverageAlphaVariant alphaVariant =
			bucket == RenderBucket::AlphaTest ? DepthCoverageAlphaVariant::BaseColorMask
			: DepthCoverageAlphaVariant::Opaque;
		return gglab::BuildDepthCoveragePipelineSignature(coverageKey,
			DepthCoverageVertexProgram::RigidMesh, DepthCoverageDeformationVariant::Rigid,
			DepthCoveragePositionPrecision::Float32, RHIFormat::R32G32B32Float, doubleSided,
			alphaVariant);
	}

	GraphicsLogicalPipelineMetadata RenderPassForwardPBRBase::
		BuildLogicalPipelineMetadataForVariant(
			const GraphicsPhysicalPipelineKey& physicalKey, uint64_t variantBits) noexcept
	{
		return {
			.m_DepthCoveragePipelineSignature =
				BuildDepthCoveragePipelineSignatureForVariant(physicalKey, variantBits),
		};
	}

	GraphicsPipelineDescription RenderPassForwardPBRBase::DescribePipelineVariant(
		uint64_t variantBits) const noexcept
	{
		GGLAB_ASSERT(m_IsInitialized);
		GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);

		GraphicsPhysicalPipelineKey physicalKey =
			m_BasePhysicalKeys[static_cast<size_t>(ForwardPBRLightingVariant::Legacy)][0];
		auto [rasterizerPreset, depthPreset, blendPreset] =
			GetPresetsFromVariantBits(variantBits, true);
		physicalKey.m_RasterizerPreset = rasterizerPreset;
		physicalKey.m_DepthPreset = depthPreset;
		physicalKey.m_BlendPreset = blendPreset;
		return {
			.m_PhysicalKey = physicalKey,
			.m_LogicalMetadata = BuildLogicalPipelineMetadataForVariant(physicalKey, variantBits),
		};
	}

	std::tuple<RasterizerPreset, DepthPreset, BlendPreset> RenderPassForwardPBRBase::
		GetPresetsFromVariantBits(uint64_t variantBits, bool useDepthEqual) const noexcept
	{
		const bool doubleSided = RenderQueueBuilder::DecodeVariantDoubleSided(variantBits);
		const auto renderBucket = RenderQueueBuilder::DecodeVariantBucket(variantBits);

		RasterizerPreset rasterizerPreset =
			doubleSided ? RasterizerPreset::TwoSided : RasterizerPreset::Default;
		BlendPreset blendPreset = BlendPreset::Default;
		DepthPreset depthPreset =
			useDepthEqual ? DepthPreset::ReversedZEqualReadOnly : DepthPreset::ReversedZWrite;

		if (renderBucket == RenderBucket::Transparent)
		{
			blendPreset = BlendPreset::AlphaBlend;
			depthPreset = DepthPreset::ReversedZReadOnly;
		}

		return { rasterizerPreset, depthPreset, blendPreset };
	}

}
