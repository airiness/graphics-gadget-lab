#include "Graphics/RenderPass/RenderPassForwardOpaque.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/DepthCoverage.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlus.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPass/ForwardPlusGraphResources.h"
#include "Graphics/RenderPass/ForwardPlusValidationGraphResources.h"
#include "Graphics/RenderPass/ForwardShadingCommon.h"
#include "Graphics/RenderPass/GTAOGraphResources.h"
#include "Graphics/Resource/RenderResourceRegistry.h"

#include <algorithm>
#include <array>
#include <memory>
#include <span>

namespace gglab
{
	namespace
	{
		constexpr std::array OpaqueBuckets{ RenderBucket::Opaque, RenderBucket::AlphaTest };
		// Scene color, HDR-diff reference, GTAO contribution and material diagnostics.
		constexpr size_t MaxOpaqueAttachmentCount = 3 + forward_shading::MaterialDiagnosticTargetCount;

		struct PassData
		{
			forward_shading::SceneInputs m_Scene{};
			RGTextureId m_GTAOFinalAO{};
			RGTextureId m_GTAOContribution{};
			RGTextureId m_AllLightsReferenceColor{};
			RGBufferId m_TileHeaders{};
			RGBufferId m_TileIndices{};
			RGTextureViewId m_GTAOFinalAOSrv{};
			RGTextureViewId m_GTAOContributionRtv{};
			RGTextureViewId m_AllLightsReferenceRtv{};
			ForwardPlusTileGrid m_ForwardPlusTileGrid{};
			bool m_HdrDiffValidation = false;
			bool m_GTAOEnabled = false;
			bool m_GTAOContributionOutputEnabled = false;
		};
	}

	void RenderPassForwardOpaque::Prepare(
		const RenderServices& services, const ForwardPBRShaderSet& shaderSet) noexcept
	{
		if (!m_IsInitialized)
		{
			GGLAB_ASSERT_MSG(
				shaderSet.IsValid(), "Forward opaque shading requires the shared Forward shader set.");
			if (!shaderSet.IsValid())
			{
				return;
			}

			m_IncludesHdrDiffValidation = shaderSet.m_IncludesHdrDiffValidation;
			const RHIBindingLayoutHandle bindingLayout =
				forward_shading::CreateBindingLayout(services, true);
			const auto prepareRecipe = [&](LightingRecipe lighting, const ForwardOpaquePixelShaders& shaders,
				uint32_t lightingTargetCount)
				{
					auto& keys = m_PhysicalKeys[static_cast<size_t>(lighting)];
					GraphicsPhysicalPipelineKey shadingKey = forward_shading::MakeBasePhysicalKey(
						bindingLayout, shaderSet.m_CoverageVertexShader, shaders.m_Shading);
					// The validation recipe also writes the all-lights reference color.
					for (uint32_t target = 1; target < lightingTargetCount; ++target)
					{
						shadingKey.m_Formats.m_RenderTargetFormats[target] = RHIFormat::R16G16B16A16Float;
					}
					shadingKey.m_Formats.m_RenderTargetCount = lightingTargetCount;
					keys[0][0] = shadingKey;

					GraphicsPhysicalPipelineKey& contributionKey = keys[1][0];
					contributionKey = shadingKey;
					contributionKey.m_PSId = shaders.m_GTAOContribution;
					contributionKey.m_Formats.m_RenderTargetFormats[
						contributionKey.m_Formats.m_RenderTargetCount++] = RHIFormat::R16G16B16A16Float;
				};
			prepareRecipe(LightingRecipe::ForwardPlus, shaderSet.m_ForwardPlus, 1);
			// Validation recipes exist only in pipelines composed for HDR-diff.
			if (m_IncludesHdrDiffValidation)
			{
				prepareRecipe(LightingRecipe::ForwardPlusValidation, shaderSet.m_ForwardPlusValidation, 2);
			}
			m_IsInitialized = true;
		}
		if (!m_MaterialDiagnosticPipelineSlots && shaderSet.AreMaterialDiagnosticsValid())
		{
			const auto prepareDiagnostics = [this](LightingRecipe lighting,
				const ForwardOpaquePixelShaders& shaders)
				{
					auto& keys = m_PhysicalKeys[static_cast<size_t>(lighting)];
					const std::array diagnosticShaders{
						shaders.m_MaterialDiagnostics, shaders.m_GTAOContributionMaterialDiagnostics };
					for (size_t contribution = 0; contribution < diagnosticShaders.size(); ++contribution)
					{
						GraphicsPhysicalPipelineKey& key = keys[contribution][1];
						key = keys[contribution][0];
						key.m_PSId = diagnosticShaders[contribution];
						forward_shading::AppendMaterialDiagnosticTargets(key);
					}
				};
			prepareDiagnostics(LightingRecipe::ForwardPlus, shaderSet.m_ForwardPlus);
			if (m_IncludesHdrDiffValidation)
			{
				prepareDiagnostics(LightingRecipe::ForwardPlusValidation, shaderSet.m_ForwardPlusValidation);
			}
			m_MaterialDiagnosticPipelineSlots = std::make_unique<PipelineSlotTable>();
		}
	}

	void RenderPassForwardOpaque::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_IsInitialized, "Forward opaque pass must be prepared before graph construction.");
		const auto* contextPtr = &context;
		const RenderViewID displayViewId = context.GetDisplayViewId();
		const auto* hdrDiffValidation = rg.GetBlackboard().TryGet<RGForwardPlusValidationResources>(
			ForwardPlusValidationResourcesName);
		const bool hdrDiffValidationActive = hdrDiffValidation && hdrDiffValidation->IsActive();
		GGLAB_ASSERT_MSG(!hdrDiffValidationActive || m_IncludesHdrDiffValidation,
			"An active HDR-diff record requires the composed validation recipe.");
		auto* registry = services.m_Resources;
		GGLAB_ASSERT_NOT_NULL(registry);
		const bool gtaoContributionRequested =
			registry->IsPostProcessPreviewRequested(PostProcessPreviewChannel::AmbientOcclusion) &&
			registry->GetPostProcessPreviewSelection(PostProcessPreviewChannel::AmbientOcclusion).m_Tap ==
				PostProcessDebugTap::GTAOAOOnlyLightingContribution;

		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[contextPtr, services, displayViewId, hdrDiffValidationActive, gtaoContributionRequested](
				RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();
				forward_shading::DeclareSceneInputs(builder, *contextPtr, services, displayViewId,
					forward_shading::CompositionDomain::PreTemporal, data.m_Scene);

				auto& blackboard = builder.GetBlackboard();
				GGLAB_ASSERT_MSG(blackboard.Get<DepthCoverageFramePlan>(DepthCoverageFramePlanName)
					.AddsForwardOpaquePass(),
					"Forward opaque shading is added only for validated frame plans with coverage draws.");

				const auto* forwardPlus = blackboard.TryGet<RGForwardPlusResources>(ForwardPlusResourcesName);
				GGLAB_ASSERT_MSG(forwardPlus && forwardPlus->IsValid(),
					"Opaque Forward shading requires this frame's culled Forward+ light lists.");
				data.m_ForwardPlusTileGrid = forwardPlus->m_TileGrid;
				data.m_TileHeaders = builder.Read(forwardPlus->m_TileLightHeaders,
					RGBufferAccess::StructuredRead, RHIStage::PixelShader);
				data.m_TileIndices = builder.Read(forwardPlus->m_TileLightIndices,
					RGBufferAccess::StructuredRead, RHIStage::PixelShader);

				// Copied: creating graph textures may invalidate builder-owned descriptors.
				const RHITextureDesc sceneColorDesc = builder.GetTextureDesc(data.m_Scene.m_SceneColor);
				const auto* gtao = blackboard.TryGet<RGGTAOResources>(GTAOResourcesName);
				if (gtao && gtao->IsComplete())
				{
					const RHITextureDesc& gtaoDesc = builder.GetTextureDesc(gtao->m_FinalAO);
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
							builder.CreateView<RHITextureViewType::RenderTarget>(data.m_GTAOContribution);
						data.m_GTAOContributionOutputEnabled = true;
					}
				}

				data.m_HdrDiffValidation = hdrDiffValidationActive;
				if (data.m_HdrDiffValidation)
				{
					auto& validation = blackboard.Get<RGForwardPlusValidationResources>(
						ForwardPlusValidationResourcesName);
					GGLAB_ASSERT_MSG(validation.IsActive() && !validation.IsValid(),
						"The validation recipe publishes one active record per frame for one opaque reference.");
					validation.m_AllLightsReferenceColor = builder.CreateTexture(
						"ForwardPlus.AllLightsReferenceColor", sceneColorDesc);
					builder.WriteInPlace(validation.m_AllLightsReferenceColor, RGTextureAccess::RenderTarget);
					data.m_AllLightsReferenceColor = validation.m_AllLightsReferenceColor;
					data.m_AllLightsReferenceRtv =
						builder.CreateView<RHITextureViewType::RenderTarget>(data.m_AllLightsReferenceColor);
				}
			},
			[this, contextPtr, services, displayViewId](RGExecuteContext& executeContext, PassData& data)
			{
				auto* graphicsContext = executeContext.GetGraphicsCommandContext();
				GGLAB_ASSERT_NOT_NULL(graphicsContext);

				std::array<RHIRenderingAttachment, MaxOpaqueAttachmentCount> renderTargets{};
				renderTargets[0] = forward_shading::GetSceneColorAttachment(executeContext, data.m_Scene);
				uint32_t renderTargetCount = 1;
				if (data.m_HdrDiffValidation)
				{
					renderTargets[renderTargetCount] = {
						.m_View = executeContext.GetViewHandle(data.m_AllLightsReferenceRtv),
						.m_LoadOp = RHIContentLoadOp::DontCare,
					};
					GGLAB_ASSERT_MSG(renderTargets[renderTargetCount].m_View.IsValid(),
						"Forward+ HDR diff requires an all-lights reference render target.");
					++renderTargetCount;
				}
				const uint32_t contributionTarget = renderTargetCount;
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
				forward_shading::AppendMaterialDiagnosticAttachments(
					executeContext, data.m_Scene, renderTargets, renderTargetCount);
				graphicsContext->BeginRendering({
					.m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(renderTargets.data(), renderTargetCount),
					.m_DepthAttachment = forward_shading::GetDepthAttachment(executeContext, data.m_Scene),
				});
				if (data.m_HdrDiffValidation)
				{
					graphicsContext->ClearColorAttachment(1, { 0.0f, 0.0f, 0.0f, 1.0f });
				}
				if (data.m_GTAOContributionOutputEnabled)
				{
					graphicsContext->ClearColorAttachment(contributionTarget, { 0.0f, 0.0f, 0.0f, 1.0f });
				}

				forward_shading::PassParameters passParameters =
					forward_shading::ResolveScenePassParameters(
						executeContext, data.m_Scene, services, displayViewId);
				if (data.m_GTAOEnabled)
				{
					const auto gtaoSrv = executeContext.GetViewDescriptor(data.m_GTAOFinalAOSrv);
					GGLAB_ASSERT_MSG(gtaoSrv.IsValid(),
						"GTAO FinalAO must expose a shader-resource descriptor before opaque shading.");
					passParameters.m_GTAOTextureIndex = gtaoSrv.m_Index;
					passParameters.m_GTAOFlags = forward_shading::GTAOEnabledFlag;
				}
				const auto& globalLightIndices = contextPtr->m_RenderScene.m_GlobalLightIndices;
				GGLAB_ASSERT_MSG(IsForwardPlusGlobalLightCountSupported(
					static_cast<uint32_t>(globalLightIndices.size())),
					"Forward+ opaque shading requires a bounded global-light list.");
				std::array<uint32_t, ForwardPlusGlobalLightCapacity> packedGlobalLightIndices{};
				std::ranges::copy(globalLightIndices, packedGlobalLightIndices.begin());
				passParameters.m_ForwardPlusTileCountX = data.m_ForwardPlusTileGrid.m_TileCountX;
				passParameters.m_ForwardPlusTileCountY = data.m_ForwardPlusTileGrid.m_TileCountY;
				passParameters.m_ForwardPlusGlobalLightCount = static_cast<uint32_t>(globalLightIndices.size());
				passParameters.m_ForwardPlusGlobalLightIndices01 = {
					packedGlobalLightIndices[0], packedGlobalLightIndices[1] };
				passParameters.m_ForwardPlusGlobalLightIndices23 = {
					packedGlobalLightIndices[2], packedGlobalLightIndices[3] };

				const auto& renderQueue = contextPtr->GetRenderQueue(displayViewId);
				const DrawItemsRange* firstDrawRange =
					forward_shading::FindFirstDrawRange(renderQueue, OpaqueBuckets);
				if (!firstDrawRange)
				{
					return;
				}
				const OutputVariant output{
					.m_Lighting = data.m_HdrDiffValidation
						? LightingRecipe::ForwardPlusValidation : LightingRecipe::ForwardPlus,
					.m_GTAOContribution = data.m_GTAOContributionOutputEnabled,
					.m_MaterialDiagnostics = data.m_Scene.m_MaterialDiagnostics,
				};
				const auto resolvePipeline = [&](uint64_t variantBits)
					{
						return GetOrCreatePSOForVariant(services, variantBits, output);
					};
				graphicsContext->SetPipeline(
					resolvePipeline(renderQueue.m_DrawItems[firstDrawRange->m_Start].m_VariantBits));
				forward_shading::BindSceneResources(
					*graphicsContext, *contextPtr, services, data.m_Scene, renderQueue);

				const RHIBufferHandle tileHeaders = executeContext.GetBufferHandle(data.m_TileHeaders);
				const RHIBufferHandle tileIndices = executeContext.GetBufferHandle(data.m_TileIndices);
				GGLAB_ASSERT_MSG(tileHeaders.IsValid() && tileIndices.IsValid(),
					"Forward+ tile buffers must resolve before opaque shading.");
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(forward_shading::RootParameter::TileHeaders), tileHeaders);
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(forward_shading::RootParameter::TileIndices), tileIndices);
				graphicsContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), passParameters);

				forward_shading::DrawBuckets(*graphicsContext, renderQueue, OpaqueBuckets,
					data.m_Scene.m_ExpectedRenderQueue, resolvePipeline);
			});
	}

	bool RenderPassForwardOpaque::PrewarmMaterialDiagnosticVariant(const RenderServices& services,
		uint64_t variantBits, bool hdrDiffValidation, bool gtaoContributionOutput) noexcept
	{
		return GetOrCreatePSOForVariant(services, variantBits, {
			.m_Lighting = hdrDiffValidation ? LightingRecipe::ForwardPlusValidation : LightingRecipe::ForwardPlus,
			.m_GTAOContribution = gtaoContributionOutput,
			.m_MaterialDiagnostics = true,
			}).IsValid();
	}

	RHIPipelineHandle RenderPassForwardOpaque::GetOrCreatePSOForVariant(const RenderServices& services,
		uint64_t variantBits, const OutputVariant& output) noexcept
	{
		GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);
		GGLAB_ASSERT_MSG(RenderQueueBuilder::DecodeVariantBucket(variantBits) != RenderBucket::Transparent,
			"Forward opaque shading only draws opaque and alpha-tested buckets.");
		auto* pipelineCache = services.m_PipelineResolver;
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		GGLAB_ASSERT_MSG(!output.m_MaterialDiagnostics || m_MaterialDiagnosticPipelineSlots,
			"Diagnostic MRT pipelines must be prepared before graph execution.");

		const size_t lighting = static_cast<size_t>(output.m_Lighting);
		const size_t contribution = output.m_GTAOContribution ? 1u : 0u;
		GraphicsPhysicalPipelineKey physicalKey =
			m_PhysicalKeys[lighting][contribution][output.m_MaterialDiagnostics ? 1u : 0u];
		GGLAB_ASSERT_MSG(physicalKey.m_PSId.IsValid(),
			"Forward opaque lighting recipe was not composed into this pipeline.");
		physicalKey.m_RasterizerPreset = forward_shading::GetRasterizerPreset(variantBits);
		physicalKey.m_DepthPreset = DepthPreset::ReversedZEqualReadOnly;
		physicalKey.m_BlendPreset = BlendPreset::Default;

		const size_t slotIndex = static_cast<size_t>(variantBits & RenderQueueBuilder::VariantMask);
		auto& slots = output.m_MaterialDiagnostics ? *m_MaterialDiagnosticPipelineSlots : m_PipelineSlots;
		return pipelineCache->Resolve(slots[lighting][contribution][slotIndex], physicalKey, GetInfo());
	}

	std::optional<DepthCoveragePipelineSignature> RenderPassForwardOpaque::
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
		coverageKey.m_RasterizerPreset = forward_shading::GetRasterizerPreset(variantBits);
		const DepthCoverageAlphaVariant alphaVariant =
			bucket == RenderBucket::AlphaTest ? DepthCoverageAlphaVariant::BaseColorMask
			: DepthCoverageAlphaVariant::Opaque;
		return gglab::BuildDepthCoveragePipelineSignature(coverageKey,
			DepthCoverageVertexProgram::RigidMesh, DepthCoverageDeformationVariant::Rigid,
			DepthCoveragePositionPrecision::Float32, RHIFormat::R32G32B32Float, doubleSided,
			alphaVariant);
	}

	GraphicsLogicalPipelineMetadata RenderPassForwardOpaque::BuildLogicalPipelineMetadataForVariant(
		const GraphicsPhysicalPipelineKey& physicalKey, uint64_t variantBits) noexcept
	{
		return {
			.m_DepthCoveragePipelineSignature =
				BuildDepthCoveragePipelineSignatureForVariant(physicalKey, variantBits),
		};
	}

	GraphicsPipelineDescription RenderPassForwardOpaque::DescribePipelineVariant(
		uint64_t variantBits) const noexcept
	{
		GGLAB_ASSERT(m_IsInitialized);
		GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);

		GraphicsPhysicalPipelineKey physicalKey =
			m_PhysicalKeys[static_cast<size_t>(LightingRecipe::ForwardPlus)][0][0];
		physicalKey.m_RasterizerPreset = forward_shading::GetRasterizerPreset(variantBits);
		physicalKey.m_DepthPreset = DepthPreset::ReversedZEqualReadOnly;
		physicalKey.m_BlendPreset = BlendPreset::Default;
		return {
			.m_PhysicalKey = physicalKey,
			.m_LogicalMetadata = BuildLogicalPipelineMetadataForVariant(physicalKey, variantBits),
		};
	}
}
