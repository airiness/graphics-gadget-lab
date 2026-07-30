#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassForwardPBR.h"
#include "Graphics/Renderer.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RenderPass/IBLGraphResources.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPass/ShadowGraphResources.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHITextureViewDescUtils.h"
#include "Graphics/SamplerRegistry.h"

namespace gglab
{
	namespace
	{
		struct ForwardPBRPassParameters
		{
			uint32_t ViewIndex = 0;
			uint32_t ShadowMapTextureIndex = 0;
			uint32_t ShadowMapSamplerIndex = 0;
			uint32_t ShadowMapSize = 0;
			uint32_t ShadowFlags = 0;
			float ShadowReceiverDepthBias = 0.0f;
			uint32_t ShadowViewIndex = 0;
			uint32_t Padding = 0;
		};
		static_assert(IsPassRootConstantStruct<ForwardPBRPassParameters>);
		static_assert(sizeof(ForwardPBRPassParameters) == 32);

		struct PassData
		{
			RGTextureId m_SceneColor{};
			RGTextureId m_Depth{};
			RGTextureId m_IrradianceCubemap{};
			RGTextureId m_PrefilteredSpecularCubemap{};
			RGTextureId m_BrdfLut{};
			RGTextureId m_ShadowMap{};

			RGTextureViewId m_Rtv{};
			RGTextureViewId m_Dsv{};
			RGTextureViewId m_ShadowSrv{};

			const DepthCoverageRasterDomain* m_RasterDomain = nullptr;
			const RenderQueue* m_ExpectedRenderQueue = nullptr;
			std::array<
				bool,
				RenderQueueBuilder::VariantCount>
				m_DepthEqualAllowed{};
			bool m_UseDepthEqual = false;
			uint32_t m_ShadowMapSize = 0;
			uint32_t m_ShadowSamplerIndex = 0;
			uint32_t m_ShadowFlags = 0;
			float m_ShadowReceiverDepthBias = 0.0f;
		};
	}

	void RenderPassForwardPBR::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		Prepare(services);
		AddBucketPass(rg, context, services, false);
		AddBucketPass(rg, context, services, true);
	}

	void RenderPassForwardPBR::AddBucketPass(
		RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services,
		bool transparent) noexcept
	{
		auto* contextPtr = &context;
		GGLAB_ASSERT_NOT_NULL(contextPtr);

		auto* servicesPtr = &services;
		GGLAB_ASSERT_NOT_NULL(servicesPtr);

		const RenderViewID displayViewId = context.GetDisplayViewId();
		const std::string passName = MakeRenderGraphPassName(
			transparent ? "Transparent" : "Opaque");

		rg.AddPass<PassData>(passName.c_str(),
			[this, contextPtr, servicesPtr, displayViewId, transparent](
				RenderGraph::RGBuilder& builder,
				PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();

				auto& targetsTable = blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& displayTargets = targetsTable.GetViewTargets(displayViewId);
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);
				auto& shadowRes = blackboard.Get<RGShadowResources>(ShadowResourcesName);
				auto& sceneDepth = blackboard.Get<RGSceneDepthResources>(
					SceneDepthResourcesName);
				const auto& coverageContract =
					blackboard.Get<RGDepthCoverageContract>(
						DepthCoverageContractName);
				const auto& renderQueue =
					contextPtr->GetRenderQueue(displayViewId);

				builder.ReadWriteInPlace(displayTargets.m_SceneColor, RGTextureAccess::RenderTarget);
				data.m_SceneColor = displayTargets.m_SceneColor;
				data.m_IrradianceCubemap = builder.Read(iblRes.m_IrradianceCubemap, RGTextureAccess::Sample);
				data.m_PrefilteredSpecularCubemap = builder.Read(iblRes.m_PrefilteredSpecularCubemap, RGTextureAccess::Sample);
				data.m_BrdfLut = builder.Read(iblRes.m_BrdfLut, RGTextureAccess::Sample);
				data.m_ShadowMap = builder.Read(shadowRes.m_DirectionalShadowMap, RGTextureAccess::Sample);

				data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(data.m_SceneColor);

				const auto shadowSrvDesc = MakeRHITexture2DViewDesc(
					RHIFormat::R32Float,
					0,
					1,
					RHITextureAspect::Depth);
				data.m_ShadowSrv =
					builder.CreateView<RHITextureViewType::ShaderResource>(data.m_ShadowMap, shadowSrvDesc);

				data.m_RasterDomain =
					std::addressof(
						renderQueue.m_CoverageRasterDomain);
				data.m_ExpectedRenderQueue =
					coverageContract.m_SourceRenderQueue;
				data.m_ShadowMapSize = shadowRes.m_ShadowMapSize;

				if (!renderQueue.m_DrawItems.empty())
				{
					GGLAB_ASSERT_MSG(
						data.m_RasterDomain->IsValid(),
						"Forward rendering requires a valid coverage raster domain.");
					GGLAB_ASSERT_MSG(
						data.m_RasterDomain->m_DepthConvention ==
							sceneDepth.m_Convention,
						"Forward raster and resource depth conventions must match.");
					GGLAB_ASSERT_MSG(
						AreDepthCoverageTargetExtentsCompatible(
							*data.m_RasterDomain,
							builder.GetTextureDesc(
								displayTargets.m_SceneColor),
							builder.GetTextureDesc(
								sceneDepth.m_Texture)),
						"Forward color and depth extents must match the coverage raster domain.");
					GGLAB_ASSERT_MSG(
						coverageContract.m_RasterDomain ==
							data.m_RasterDomain,
						"Prepass and Forward must share one raster-domain instance.");
					GGLAB_ASSERT_MSG(
						data.m_ExpectedRenderQueue ==
							std::addressof(renderQueue),
						"Prepass and Forward must consume one RenderQueue and its shared draw packets.");
				}

				if (!transparent)
				{
					bool allCoverageMatches = true;
					const auto& ranges =
						renderQueue.m_BucketDrawRanges;
					for (const RenderBucket bucket :
						{ RenderBucket::Opaque,
							RenderBucket::AlphaTest })
					{
						const DrawItemsRange range =
							ranges[utils::ToIndex(bucket)];
						for (uint32_t offset = 0;
							offset < range.m_Count;
							++offset)
						{
							const DrawItem& drawItem =
								renderQueue.m_DrawItems[
									range.m_Start + offset];
							const size_t variantIndex =
								static_cast<size_t>(
									drawItem.m_VariantBits &
										RenderQueueBuilder::VariantMask);
							if (data.m_DepthEqualAllowed[
								variantIndex])
							{
								continue;
							}

							const auto& prepassSignature =
								coverageContract.
									m_PrepassPipelineSignatures[
										variantIndex];
							const auto forwardSignature =
								DescribePipelineVariant(
									drawItem.m_VariantBits).
									m_LogicalMetadata.
									m_DepthCoveragePipelineSignature;
							const bool sharedDomainAndPackets =
								coverageContract.m_RasterDomain ==
									data.m_RasterDomain &&
								data.m_ExpectedRenderQueue ==
									std::addressof(renderQueue);
							data.m_DepthEqualAllowed[
								variantIndex] =
								sharedDomainAndPackets &&
								ValidateDepthEqualVariant(
									drawItem.m_VariantBits,
									prepassSignature,
									forwardSignature);
							allCoverageMatches =
								allCoverageMatches &&
								data.m_DepthEqualAllowed[
									variantIndex];
							GGLAB_ASSERT_MSG(
								data.m_DepthEqualAllowed[
									variantIndex],
								"DepthEqual requires matching prepass and Forward pipeline, raster-domain, and draw-packet contracts.");
						}
					}
					data.m_UseDepthEqual =
						allCoverageMatches;
				}

				if (transparent || data.m_UseDepthEqual)
				{
					data.m_Depth = builder.Read(
						sceneDepth.m_Texture,
						RGTextureAccess::DepthStencilRead);
					RHITextureViewDesc readOnlyDsvDesc =
						sceneDepth.m_DsvDesc;
					readOnlyDsvDesc.m_ReadOnlyDepth = true;
					data.m_Dsv =
						builder.CreateView<
							RHITextureViewType::DepthStencil>(
							data.m_Depth,
							readOnlyDsvDesc);
				}
				else
				{
					builder.ReadWriteInPlace(
						sceneDepth.m_Texture,
						RGTextureAccess::DepthStencilWrite);
					data.m_Depth = sceneDepth.m_Texture;
					data.m_Dsv =
						builder.CreateView<
							RHITextureViewType::DepthStencil>(
							data.m_Depth,
							sceneDepth.m_DsvDesc);
				}

				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);
				data.m_ShadowSamplerIndex = renderer->GetSamplerRegistry()->GetSamplerIndex(
					SamplerPreset::ShadowCmpLinearClamp);

				const auto& shadowSettings = contextPtr->GetDirectionalShadowSettings();
				data.m_ShadowFlags =
					(shadowSettings.m_Enable ? 1u : 0u) |
					(shadowSettings.m_EnablePCF ? 2u : 0u);
				data.m_ShadowReceiverDepthBias = shadowSettings.m_ReceiverDepthBias;
			},
			[this, contextPtr, servicesPtr, displayViewId, transparent](
				RGExecuteContext& executeContext,
				PassData& data)
			{
				auto* graphicsContext = executeContext.GetGraphicsCommandContext();
				GGLAB_ASSERT_NOT_NULL(graphicsContext);

				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				const auto dsv = executeContext.GetViewHandle(data.m_Dsv);
				graphicsContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&rtv, 1), dsv);

				const auto shadowSrv = executeContext.GetViewDescriptor(data.m_ShadowSrv);
				GGLAB_ASSERT_MSG(shadowSrv.IsValid(),
					"ForwardPBR shadow map SRV must expose a descriptor heap index.");

				auto* renderer = servicesPtr->m_Renderer;
				const auto& renderQueue = contextPtr->GetRenderQueue(displayViewId);
				if (renderQueue.m_DrawItems.empty())
				{
					return;
				}
				GGLAB_ASSERT_NOT_NULL(data.m_RasterDomain);
				GGLAB_ASSERT_MSG(
					data.m_RasterDomain ==
						std::addressof(
							renderQueue.m_CoverageRasterDomain),
					"Forward must consume the RenderQueue raster domain directly.");
				graphicsContext->SetViewport(
					data.m_RasterDomain->m_Viewport);
				graphicsContext->SetScissorRect(
					data.m_RasterDomain->m_Scissor);
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
					objectSB->GetBufferHandle(contextPtr->m_BackBufferIndex));

				// Set material structured buffer
				const auto& materialSB = renderer->GetMaterialStructuredBuffer();
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB),
					materialSB->GetBufferHandle(contextPtr->m_BackBufferIndex));

				// View structured buffer
				const auto& viewSB = renderer->GetViewStructuredBuffer();
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					viewSB->GetBufferHandle());

				// Light structured buffer
				const auto& lightSB = renderer->GetLightStructuredBuffer();
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::LightSB),
					lightSB->GetBufferHandle(contextPtr->m_BackBufferIndex));

				const ForwardPBRPassParameters passParameters{
					.ViewIndex = static_cast<uint32_t>(utils::ToIndex(displayViewId)),
					.ShadowMapTextureIndex = shadowSrv.m_Index,
					.ShadowMapSamplerIndex = data.m_ShadowSamplerIndex,
					.ShadowMapSize = data.m_ShadowMapSize,
					.ShadowFlags = data.m_ShadowFlags,
					.ShadowReceiverDepthBias = data.m_ShadowReceiverDepthBias,
					.ShadowViewIndex = static_cast<uint32_t>(utils::ToIndex(RenderViewID::DirectionalShadow)),
				};
				graphicsContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
					passParameters);

				DrawRenderQueue(
					graphicsContext,
					*contextPtr,
					*servicesPtr,
					displayViewId,
					transparent,
					data.m_ExpectedRenderQueue,
					data.m_UseDepthEqual);
			});
	}

	void RenderPassForwardPBR::Prepare(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			// Shader
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Passes/PassForwardPBR.hlsl";
			shaderDesc.m_Stage = ShaderStage::Vertex;
			shaderDesc.m_Entry = L"VSMain";
			const auto vsId = shaderManager->LoadShader(shaderDesc);
			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSMain";
			const auto psId = shaderManager->LoadShader(shaderDesc);

			// Pipeline recipe
			m_BasePhysicalKey.m_BindingLayout =
				renderer->GetCommonBindingLayout();
			m_BasePhysicalKey.m_InputLayoutId =
				InputLayoutID::P3N3T2T2Tan4;
			m_BasePhysicalKey.m_VSId = vsId;
			m_BasePhysicalKey.m_PSId = psId;

			m_BasePhysicalKey.m_TopologyType =
				RHIPrimitiveTopologyType::Triangle;
			m_BasePhysicalKey.m_PrimitiveTopology =
				RHIPrimitiveTopology::TriangleList;
			m_BasePhysicalKey.m_Formats.m_RenderTargetFormats[0] =
				RHIFormat::R16G16B16A16Float;
			m_BasePhysicalKey.m_Formats.m_RenderTargetCount = 1;
			m_BasePhysicalKey.m_Formats.m_DepthStencilFormat =
				RHIFormat::D32Float;
			m_BasePhysicalKey.m_Formats.m_SampleCount = 1;
			m_BasePhysicalKey.m_Formats.m_SampleQuality = 0;
			m_BasePhysicalKey.m_RasterizerPreset =
				RasterizerPreset::Default;
			m_BasePhysicalKey.m_BlendPreset = BlendPreset::Default;
			m_BasePhysicalKey.m_DepthPreset =
				DepthPreset::ReversedZWrite;

			m_IsInitialized = true;
		}

	}

	void RenderPassForwardPBR::DrawRenderQueue(RHIGraphicsCommandContext* graphicsContext,
		const RenderFrameContext& context,
		const RenderServices& services,
		RenderViewID viewId,
		bool transparent,
		const RenderQueue* expectedRenderQueue,
		bool useDepthEqual) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(graphicsContext);
		const auto& renderQueue = context.GetRenderQueue(viewId);
		if (renderQueue.m_DrawItems.empty())
		{
			return;
		}
		GGLAB_ASSERT_MSG(
			renderQueue.m_CoverageRasterDomain.IsValid(),
			"Forward depth coverage requires one valid raster domain per view.");
		const auto ranges = renderQueue.m_BucketDrawRanges;

		if (transparent)
		{
			DrawRange(
				graphicsContext,
				services,
				renderQueue,
				ranges[utils::ToIndex(RenderBucket::Transparent)],
				false,
				expectedRenderQueue);
			return;
		}

		for (const RenderBucket bucket :
			{ RenderBucket::Opaque, RenderBucket::AlphaTest })
		{
			const DrawItemsRange range =
				ranges[utils::ToIndex(bucket)];
			if (range.m_Count == 0)
			{
				continue;
			}
			DrawRange(
				graphicsContext,
				services,
				renderQueue,
				range,
				useDepthEqual,
				expectedRenderQueue);
		}
	}

	void RenderPassForwardPBR::DrawRange(RHIGraphicsCommandContext* graphicsContext,
		const RenderServices& services,
		const RenderQueue& renderQueue,
		const DrawItemsRange& range,
		bool useDepthEqual,
		const RenderQueue* expectedRenderQueue) noexcept
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
			const DepthCoverageDrawPacket& drawPacket =
				drawItem.m_CoverageDrawPacket;
			GGLAB_ASSERT_MSG(
				drawPacket.IsValid(),
				"Forward received an incomplete shared depth coverage draw packet.");
			GGLAB_ASSERT_MSG(
				expectedRenderQueue == std::addressof(renderQueue) &&
					IsSameDepthCoverageDrawPacket(
						drawPacket,
						expectedRenderQueue->m_DrawItems[
							range.m_Start + index].
							m_CoverageDrawPacket),
				"Forward must consume the exact draw-packet instances used by the depth prepass.");

			// Set PSO
			if (drawItem.m_VariantBits != lastVariantBits)
			{
				graphicsContext->SetPipeline(
					GetOrCreatePSOForVariant(
						*services.m_Renderer,
						drawItem.m_VariantBits,
						useDepthEqual));

				lastVariantBits = drawItem.m_VariantBits;
			}

			const auto& geometry = drawPacket.m_Geometry;
			if (!hasBoundMesh || geometry.m_MeshId != lastMeshId)
			{
				graphicsContext->SetVertexBuffers(
					0,
					std::span<const RHIVertexBufferBinding>(
						&geometry.m_VertexBuffer,
						1));
				graphicsContext->SetIndexBuffer(
					geometry.m_IndexBuffer);
				lastMeshId = geometry.m_MeshId;
				hasBoundMesh = true;
			}

			graphicsContext->SetPushConstants(
				static_cast<uint32_t>(CommonRSRootParamIndex::DrawConstants),
				drawPacket.m_DrawParameters);

			const auto& draw = drawPacket.m_IndexedDraw;
			graphicsContext->DrawIndexed(
				draw.m_IndexCount,
				draw.m_InstanceCount,
				draw.m_StartIndexLocation,
				draw.m_BaseVertexLocation,
				draw.m_StartInstanceLocation);
		}
	}

	RHIPipelineHandle RenderPassForwardPBR::GetOrCreatePSOForVariant(
		const Renderer& renderer,
		uint64_t variantBits,
		bool useDepthEqual) noexcept
	{
		GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);

		GraphicsPipelineDescription description =
			DescribePipelineVariant(variantBits);
		auto [rasterizerPreset, depthPreset, blendPreset] =
			GetPresetsFromVariantBits(
				variantBits,
				useDepthEqual);
		description.m_PhysicalKey.m_RasterizerPreset =
			rasterizerPreset;
		description.m_PhysicalKey.m_DepthPreset = depthPreset;
		description.m_PhysicalKey.m_BlendPreset = blendPreset;

		const size_t slotIndex =
			static_cast<size_t>(variantBits & RenderQueueBuilder::VariantMask);
		auto& slot = m_PipelineSlots[slotIndex];
		return pipelineCache->Resolve(
			slot,
			description.m_PhysicalKey,
			GetInfo());
	}

	std::optional<DepthCoveragePipelineSignature>
		RenderPassForwardPBR::BuildDepthCoveragePipelineSignatureForVariant(
			const GraphicsPhysicalPipelineKey& physicalKey,
			uint64_t variantBits) noexcept
	{
		GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);
		const RenderBucket bucket =
			RenderQueueBuilder::DecodeVariantBucket(variantBits);
		if (bucket == RenderBucket::Transparent)
		{
			return std::nullopt;
		}

		const bool doubleSided =
			RenderQueueBuilder::DecodeVariantDoubleSided(variantBits);
		GraphicsPhysicalPipelineKey coverageKey = physicalKey;
		coverageKey.m_RasterizerPreset = doubleSided ?
			RasterizerPreset::TwoSided :
			RasterizerPreset::Default;
		const DepthCoverageAlphaVariant alphaVariant =
			bucket == RenderBucket::AlphaTest ?
				DepthCoverageAlphaVariant::BaseColorMask :
				DepthCoverageAlphaVariant::Opaque;
		return gglab::BuildDepthCoveragePipelineSignature(
			coverageKey,
			DepthCoverageVertexProgram::RigidMesh,
			DepthCoverageDeformationVariant::Rigid,
			DepthCoveragePositionPrecision::Float32,
			RHIFormat::R32G32B32Float,
			doubleSided,
			alphaVariant);
	}

	GraphicsLogicalPipelineMetadata
		RenderPassForwardPBR::BuildLogicalPipelineMetadataForVariant(
			const GraphicsPhysicalPipelineKey& physicalKey,
			uint64_t variantBits) noexcept
	{
		return {
			.m_DepthCoveragePipelineSignature =
				BuildDepthCoveragePipelineSignatureForVariant(
					physicalKey,
					variantBits),
		};
	}

	GraphicsPipelineDescription
		RenderPassForwardPBR::DescribePipelineVariant(
			uint64_t variantBits) const noexcept
	{
		GGLAB_ASSERT(m_IsInitialized);
		GGLAB_ASSERT(
			(variantBits & ~RenderQueueBuilder::VariantMask) == 0);

		GraphicsPhysicalPipelineKey physicalKey =
			m_BasePhysicalKey;
		auto [rasterizerPreset, depthPreset, blendPreset] =
			GetPresetsFromVariantBits(variantBits, true);
		physicalKey.m_RasterizerPreset = rasterizerPreset;
		physicalKey.m_DepthPreset = depthPreset;
		physicalKey.m_BlendPreset = blendPreset;
		return {
			.m_PhysicalKey = physicalKey,
			.m_LogicalMetadata =
				BuildLogicalPipelineMetadataForVariant(
					physicalKey,
					variantBits),
		};
	}

	std::tuple<RasterizerPreset, DepthPreset, BlendPreset>
		RenderPassForwardPBR::GetPresetsFromVariantBits(
			uint64_t variantBits,
			bool useDepthEqual) const noexcept
	{
		const bool doubleSided = RenderQueueBuilder::DecodeVariantDoubleSided(variantBits);
		const auto renderBucket = RenderQueueBuilder::DecodeVariantBucket(variantBits);

		RasterizerPreset rasterizerPreset = doubleSided ?
			RasterizerPreset::TwoSided :
			RasterizerPreset::Default;
		BlendPreset blendPreset = BlendPreset::Default;
		DepthPreset depthPreset = useDepthEqual ?
			DepthPreset::ReversedZEqualReadOnly :
			DepthPreset::ReversedZWrite;

		if (renderBucket == RenderBucket::Transparent)
		{
			blendPreset = BlendPreset::AlphaBlend;
			depthPreset = DepthPreset::ReversedZReadOnly;
		}

		return { rasterizerPreset, depthPreset, blendPreset };
	}

	bool RenderPassForwardPBR::ValidateDepthEqualVariant(
		uint64_t variantBits,
		const std::optional<DepthCoveragePipelineSignature>&
			prepassSignature,
		const std::optional<DepthCoveragePipelineSignature>&
			forwardSignature) noexcept
	{
		const size_t variantIndex =
			static_cast<size_t>(
				variantBits & RenderQueueBuilder::VariantMask);
		auto& validation =
			m_DepthEqualVariantValidations[variantIndex];
		if (validation.m_PrepassSignature != prepassSignature ||
			validation.m_ForwardSignature != forwardSignature)
		{
			validation.m_PrepassSignature = prepassSignature;
			validation.m_ForwardSignature = forwardSignature;
			validation.m_Matches =
				prepassSignature &&
				forwardSignature &&
				CompareDepthCoveragePipelineSignatures(
					*prepassSignature,
					*forwardSignature).m_Matches;
		}
		return validation.m_Matches;
	}

}
