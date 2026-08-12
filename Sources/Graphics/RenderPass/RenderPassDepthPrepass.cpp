#include "Graphics/RenderPass/RenderPassDepthPrepass.h"
#include "Core/CoreMacros.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RHI/RHICommandContext.h"

#include <limits>
#include <memory>
#include <optional>
#include <span>

namespace gglab
{
	namespace
	{
		struct DepthPrepassParameters
		{
			uint32_t ViewIndex = 0;
			uint32_t Padding[3]{};
		};
		static_assert(IsPassRootConstantStruct<DepthPrepassParameters>);
		static_assert(sizeof(DepthPrepassParameters) == 16);

		struct PassData
		{
			RGTextureId m_Depth{};
			RGTextureViewId m_Dsv{};
			const DepthCoverageRasterDomain* m_RasterDomain = nullptr;
			const RenderQueue* m_ExpectedRenderQueue = nullptr;
			float m_ClearDepth = 0.0f;
			bool m_DrawCoverage = false;
		};
	}

	void RenderPassDepthPrepass::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_IsInitialized, "Depth prepass must be prepared before graph construction.");

		const auto* contextPtr = &context;
		const auto* servicesPtr = &services;
		const RenderViewID displayViewId = context.GetDisplayViewId();

		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[this, contextPtr, displayViewId](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& sceneDepth = blackboard.Get<RGSceneDepthResources>(SceneDepthResourcesName);
				const auto& renderQueue = contextPtr->GetRenderQueue(displayViewId);
				const auto& framePlan =
					blackboard.Get<DepthCoverageFramePlan>(DepthCoverageFramePlanName);

				builder.WriteInPlace(sceneDepth.m_Texture, RGTextureAccess::DepthStencilWrite);
				data.m_Depth = sceneDepth.m_Texture;
				data.m_Dsv = builder.CreateView<RHITextureViewType::DepthStencil>(
					data.m_Depth, sceneDepth.m_DsvDesc);
				data.m_ClearDepth = screen_space::GetDepthBackgroundValue(sceneDepth.m_Convention);
				data.m_RasterDomain = std::addressof(renderQueue.m_CoverageRasterDomain);
				data.m_ExpectedRenderQueue = framePlan.m_SourceRenderQueue;
				data.m_DrawCoverage = framePlan.UsesDepthPrepassEqual();

				GGLAB_ASSERT_MSG(
					framePlan.UsesDepthPrepassEqual() ||
					framePlan.m_ExecutionMode == DepthCoverageExecutionMode::SkipGeometry,
					"Depth prepass can only execute the validated EQUAL path or a clear-only safety path.");
				GGLAB_ASSERT_MSG(data.m_ExpectedRenderQueue == std::addressof(renderQueue),
					"Depth prepass must consume the frame-plan RenderQueue.");
				GGLAB_ASSERT_MSG(framePlan.m_RasterDomain == data.m_RasterDomain,
					"Depth prepass must consume the frame-plan raster domain.");

				if (!data.m_DrawCoverage || renderQueue.m_DrawItems.empty())
				{
					return;
				}

				const auto& rasterDomain = renderQueue.m_CoverageRasterDomain;
				const auto& depthDesc = builder.GetTextureDesc(sceneDepth.m_Texture);
				GGLAB_ASSERT_MSG(rasterDomain.IsValid(),
					"Depth prepass requires a valid coverage raster domain.");
				GGLAB_ASSERT_MSG(rasterDomain.m_DepthConvention == sceneDepth.m_Convention,
					"Depth prepass raster and resource depth conventions must match.");
				GGLAB_ASSERT_MSG(IsDepthCoverageTargetExtentCompatible(rasterDomain, depthDesc),
					"Depth prepass target extent must match its coverage raster domain.");
			},
			[this, contextPtr, servicesPtr, displayViewId](
				RGExecuteContext& executeContext, PassData& data)
			{
				auto* graphicsContext = executeContext.GetGraphicsCommandContext();
				GGLAB_ASSERT_NOT_NULL(graphicsContext);

				const auto dsv = executeContext.GetViewHandle(data.m_Dsv);
				graphicsContext->BeginRendering({
					.m_DepthAttachment = RHIRenderingAttachment{
						.m_View = dsv,
						.m_LoadOp = RHIContentLoadOp::DontCare,
					},
				});
				graphicsContext->ClearDepthAttachment(data.m_ClearDepth);

				if (!data.m_DrawCoverage)
				{
					return;
				}

				const auto& renderQueue = contextPtr->GetRenderQueue(displayViewId);
				if (renderQueue.m_DrawItems.empty())
				{
					return;
				}

				const auto& ranges = renderQueue.m_BucketDrawRanges;
				const DrawItemsRange* firstDrawRange = nullptr;
				for (const RenderBucket bucket : {RenderBucket::Opaque, RenderBucket::AlphaTest})
				{
					const auto& range = ranges[utils::ToIndex(bucket)];
					if (range.m_Count > 0)
					{
						firstDrawRange = std::addressof(range);
						break;
					}
				}
				if (!firstDrawRange)
				{
					return;
				}
				GGLAB_ASSERT_MSG(firstDrawRange->m_Start < renderQueue.m_DrawItems.size(),
					"Depth prepass first draw must be inside the frame-plan RenderQueue.");
				if (firstDrawRange->m_Start >= renderQueue.m_DrawItems.size())
				{
					return;
				}

				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);
				graphicsContext->SetPipeline(GetOrCreatePSOForVariant(
					*renderer, renderQueue.m_DrawItems[firstDrawRange->m_Start].m_VariantBits));

				GGLAB_ASSERT_NOT_NULL(data.m_RasterDomain);
				GGLAB_ASSERT_MSG(
					data.m_RasterDomain == std::addressof(renderQueue.m_CoverageRasterDomain),
					"Depth prepass must consume the RenderQueue raster domain directly.");
				graphicsContext->SetViewport(data.m_RasterDomain->m_Viewport);
				graphicsContext->SetScissorRect(data.m_RasterDomain->m_Scissor);
				graphicsContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

				const auto* sceneBuffer = renderer->GetSceneConstantBuffer();
				graphicsContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					sceneBuffer->GetBufferHandle(),
					contextPtr->m_RenderScene.m_SceneConstantBufferOffset);
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ObjectSB),
					renderer->GetObjectStructuredBuffer()->GetBufferHandle(
						contextPtr->m_FrameSlotIndex));
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB),
					renderer->GetMaterialStructuredBuffer()->GetBufferHandle(
						contextPtr->m_FrameSlotIndex));
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					renderer->GetViewStructuredBuffer()->GetBufferHandle());

				const DepthPrepassParameters passParameters{
					.ViewIndex = static_cast<uint32_t>(utils::ToIndex(displayViewId)),
				};
				graphicsContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), passParameters);

				DrawRenderQueue(graphicsContext, *contextPtr, *servicesPtr, displayViewId);
			});
	}

	void RenderPassDepthPrepass::Prepare(
		const RenderServices& services, const ForwardPBRShaderSet& shaderSet) noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}

		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);
		GGLAB_ASSERT_MSG(
			shaderSet.IsValid(), "Depth prepass requires the shared Forward shader set.");
		if (!shaderSet.IsValid())
		{
			return;
		}
		m_AlphaTestPixelShader = shaderSet.m_AlphaTestPixelShader;

		m_BasePhysicalKey.m_BindingLayout = renderer->GetCommonBindingLayout();
		m_BasePhysicalKey.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
		m_BasePhysicalKey.m_VSId = shaderSet.m_CoverageVertexShader;
		m_BasePhysicalKey.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		m_BasePhysicalKey.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		m_BasePhysicalKey.m_Formats.m_RenderTargetCount = 0;
		m_BasePhysicalKey.m_Formats.m_DepthStencilFormat = RHIFormat::D32Float;
		m_BasePhysicalKey.m_Formats.m_SampleCount = 1;
		m_BasePhysicalKey.m_Formats.m_SampleQuality = 0;
		m_BasePhysicalKey.m_RasterizerPreset = RasterizerPreset::Default;
		m_BasePhysicalKey.m_DepthPreset = DepthPreset::ReversedZWrite;
		m_BasePhysicalKey.m_BlendPreset = BlendPreset::ColorWriteDisable;

		m_IsInitialized = true;
	}

	void RenderPassDepthPrepass::DrawRenderQueue(RHIGraphicsCommandContext* graphicsContext,
		const RenderFrameContext& context, const RenderServices& services,
		RenderViewID viewId) noexcept
	{
		const auto& renderQueue = context.GetRenderQueue(viewId);
		const auto& ranges = renderQueue.m_BucketDrawRanges;
		DrawRange(
			graphicsContext, services, renderQueue, ranges[utils::ToIndex(RenderBucket::Opaque)]);
		DrawRange(graphicsContext, services, renderQueue,
			ranges[utils::ToIndex(RenderBucket::AlphaTest)]);
	}

	void RenderPassDepthPrepass::DrawRange(RHIGraphicsCommandContext* graphicsContext,
		const RenderServices& services, const RenderQueue& renderQueue,
		const DrawItemsRange& range) noexcept
	{
		if (range.m_Count == 0)
		{
			return;
		}

		GGLAB_ASSERT_NOT_NULL(graphicsContext);
		const auto& drawItems = renderQueue.m_DrawItems;
		uint64_t lastVariantBits = std::numeric_limits<uint64_t>::max();
		MeshID lastMeshId{};
		bool hasBoundMesh = false;

		for (uint32_t offset = 0; offset < range.m_Count; ++offset)
		{
			const DrawItem& drawItem = drawItems[range.m_Start + offset];
			const DepthCoverageDrawPacket& drawPacket = drawItem.m_CoverageDrawPacket;
			GGLAB_ASSERT_MSG(drawPacket.IsValid(),
				"Depth prepass received an incomplete shared coverage draw packet.");

			if (drawItem.m_VariantBits != lastVariantBits)
			{
				graphicsContext->SetPipeline(
					GetOrCreatePSOForVariant(*services.m_Renderer, drawItem.m_VariantBits));
				lastVariantBits = drawItem.m_VariantBits;
			}

			const auto& geometry = drawPacket.m_Geometry;
			if (!hasBoundMesh || geometry.m_MeshId != lastMeshId)
			{
				graphicsContext->SetVertexBuffers(
					0, std::span<const RHIVertexBufferBinding>(
						std::addressof(geometry.m_VertexBuffer), 1));
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

	RHIPipelineHandle RenderPassDepthPrepass::GetOrCreatePSOForVariant(
		const Renderer& renderer, uint64_t variantBits) noexcept
	{
		const GraphicsPipelineDescription description = DescribePipelineVariant(variantBits);
		const size_t slotIndex = static_cast<size_t>(variantBits & RenderQueueBuilder::VariantMask);
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(
			m_PipelineSlots[slotIndex], description.m_PhysicalKey, GetInfo());
	}

	std::optional<DepthCoveragePipelineSignature> RenderPassDepthPrepass::
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

	GraphicsLogicalPipelineMetadata RenderPassDepthPrepass::BuildLogicalPipelineMetadataForVariant(
		const GraphicsPhysicalPipelineKey& physicalKey, uint64_t variantBits) noexcept
	{
		return {
			.m_DepthCoveragePipelineSignature =
				BuildDepthCoveragePipelineSignatureForVariant(physicalKey, variantBits),
		};
	}

	GraphicsPipelineDescription RenderPassDepthPrepass::DescribePipelineVariant(
		uint64_t variantBits) const noexcept
	{
		GGLAB_ASSERT(m_IsInitialized);
		GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);

		const RenderBucket bucket = RenderQueueBuilder::DecodeVariantBucket(variantBits);
		GGLAB_ASSERT(bucket != RenderBucket::Transparent);
		GraphicsPhysicalPipelineKey physicalKey = m_BasePhysicalKey;
		physicalKey.m_RasterizerPreset = RenderQueueBuilder::DecodeVariantDoubleSided(variantBits)
			? RasterizerPreset::TwoSided
			: RasterizerPreset::Default;
		if (bucket == RenderBucket::AlphaTest)
		{
			physicalKey.m_PSId = m_AlphaTestPixelShader;
		}

		return {
			.m_PhysicalKey = physicalKey,
			.m_LogicalMetadata = BuildLogicalPipelineMetadataForVariant(physicalKey, variantBits),
		};
	}
}
