#include "Graphics/RenderPass/RenderPassDirectionalShadowMap.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicConstantBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicStructuredBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/Shader/ShaderProgramCatalog.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/ShadowGraphResources.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"

#include <limits>
#include <memory>
#include <span>

namespace gglab
{
	namespace
	{
		struct DirectionalShadowPassParameters
		{
			uint32_t ViewIndex = 0;
			uint32_t Padding[3]{};
		};
		static_assert(IsPassRootConstantStruct<DirectionalShadowPassParameters>);
		static_assert(sizeof(DirectionalShadowPassParameters) == 16);

		struct PassData
		{
			RGTextureId m_ShadowMap{};
			RGTextureViewId m_Dsv{};
			const DepthCoverageRasterDomain* m_RasterDomain = nullptr;
		};

	}

	void RenderPassDirectionalShadowMap::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		auto* contextPtr = &context;
		GGLAB_ASSERT_NOT_NULL(contextPtr);


		EnsureInitialized(services);

		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[contextPtr](RenderGraph::RGBuilder& builder, PassData& data)
			{
				auto& shadowRes =
					builder.GetBlackboard().Get<RGShadowResources>(ShadowResourcesName);
				builder.WriteInPlace(
					shadowRes.m_DirectionalShadowMap, RGTextureAccess::DepthStencilWrite);
				data.m_ShadowMap = shadowRes.m_DirectionalShadowMap;
				const auto& renderQueue =
					contextPtr->GetRenderQueue(RenderViewID::DirectionalShadow);
				data.m_RasterDomain = std::addressof(renderQueue.m_CoverageRasterDomain);
				if (!renderQueue.m_DrawItems.empty())
				{
					const auto& shadowDesc = builder.GetTextureDesc(data.m_ShadowMap);
					GGLAB_ASSERT_MSG(
						data.m_RasterDomain->IsValid() &&
						IsDepthCoverageTargetExtentCompatible(*data.m_RasterDomain, shadowDesc),
						"Shadow-map extent must match its coverage raster domain.");
				}

				const auto dsvDesc =
					MakeRHITexture2DViewDesc(RHIFormat::D32Float, 0, 1, RHITextureAspect::Depth);
				data.m_Dsv =
					builder.CreateView<RHITextureViewType::DepthStencil>(data.m_ShadowMap, dsvDesc);
			},
			[this, contextPtr, services](RGExecuteContext& executeContext, PassData& data)
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
				graphicsContext->ClearDepthAttachment(1.0f);

				const auto& renderQueue =
					contextPtr->GetRenderQueue(RenderViewID::DirectionalShadow);
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
					"Directional shadow first draw must be inside its RenderQueue.");
				if (firstDrawRange->m_Start >= renderQueue.m_DrawItems.size())
				{
					return;
				}
				graphicsContext->SetPipeline(GetOrCreatePSOForVariant(services,
					renderQueue.m_DrawItems[firstDrawRange->m_Start].m_VariantBits,
					contextPtr->GetDirectionalShadowSettings()));

				GGLAB_ASSERT_NOT_NULL(data.m_RasterDomain);
				GGLAB_ASSERT_MSG(
					data.m_RasterDomain == std::addressof(renderQueue.m_CoverageRasterDomain),
					"Shadow rendering must consume the RenderQueue raster domain directly.");
				graphicsContext->SetViewport(data.m_RasterDomain->m_Viewport);
				graphicsContext->SetScissorRect(data.m_RasterDomain->m_Scissor);
				graphicsContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

				const auto* sceneBuffer = services.m_FrameBuffers->GetSceneConstantBuffer();
				graphicsContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					sceneBuffer->GetBufferHandle(),
					contextPtr->m_RenderScene.m_SceneConstantBufferOffset);

				const auto& objectSB = services.m_FrameBuffers->GetObjectStructuredBuffer();
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ObjectSB),
					objectSB->GetBufferHandle(contextPtr->m_FrameSlotIndex));

				const auto& materialSB = services.m_FrameBuffers->GetMaterialStructuredBuffer();
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB),
					materialSB->GetBufferHandle(contextPtr->m_FrameSlotIndex));

				const auto& viewSB = services.m_FrameBuffers->GetViewStructuredBuffer();
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					viewSB->GetBufferHandle());

				const DirectionalShadowPassParameters passParameters{
					.ViewIndex =
						static_cast<uint32_t>(utils::ToIndex(RenderViewID::DirectionalShadow)),
				};
				graphicsContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), passParameters);

				DrawRenderQueue(graphicsContext, *contextPtr, services);
			});
	}

	void RenderPassDirectionalShadowMap::EnsureInitialized(const RenderServices& services) noexcept
	{

		auto* shaderManager = services.m_ShaderPrograms;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			const auto vsId =
				shaderManager->LoadProgram(shader_programs::DirectionalShadowMapVertex);
			m_AlphaTestPixelShader =
				shaderManager->LoadProgram(shader_programs::DirectionalShadowMapPixel);

			m_BaseRecipe.m_BindingLayout = services.m_BindingLayout->GetCommonBindingLayout();
			m_BaseRecipe.m_InputLayoutId = InputLayoutID::MeshPositionUVs;
			m_BaseRecipe.m_VSId = vsId;
			// The opaque shadow bucket is depth-only and has no pixel shader.
			// Assign ShaderID::Invalid() explicitly: a bare "= {}" would zero-initialize
			// the TypedIndex to a *valid* id 0 (the first registered shader), which is a
			// vertex shader and would be submitted in the pixel slot (D3D12 ERROR #94).
			m_BaseRecipe.m_PSId = ShaderID::Invalid();

			m_BaseRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
			m_BaseRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
			m_BaseRecipe.m_Formats.m_RenderTargetCount = 0;
			m_BaseRecipe.m_Formats.m_DepthStencilFormat = RHIFormat::D32Float;
			m_BaseRecipe.m_Formats.m_SampleCount = 1;
			m_BaseRecipe.m_Formats.m_SampleQuality = 0;

			m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
			m_BaseRecipe.m_BlendPreset = BlendPreset::ColorWriteDisable;
			m_BaseRecipe.m_DepthPreset = DepthPreset::StandardZWrite;

			m_IsInitialized = true;
		}
	}

	void RenderPassDirectionalShadowMap::DrawRenderQueue(RHIGraphicsCommandContext* graphicsContext,
		const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(graphicsContext);
		const auto& renderQueue = context.GetRenderQueue(RenderViewID::DirectionalShadow);
		if (renderQueue.m_DrawItems.empty())
		{
			return;
		}

		const auto ranges = renderQueue.m_BucketDrawRanges;
		DrawRange(graphicsContext, context, services, renderQueue,
			ranges[utils::ToIndex(RenderBucket::Opaque)]);
		DrawRange(graphicsContext, context, services, renderQueue,
			ranges[utils::ToIndex(RenderBucket::AlphaTest)]);
	}

	void RenderPassDirectionalShadowMap::DrawRange(RHIGraphicsCommandContext* graphicsContext,
		const RenderFrameContext& context, const RenderServices& services,
		const RenderQueue& renderQueue, const DrawItemsRange& range) noexcept
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

		for (uint32_t index = 0; index < range.m_Count; ++index)
		{
			const auto& drawItem = drawItems[range.m_Start + index];
			const auto& drawPacket = drawItem.m_CoverageDrawPacket;
			GGLAB_ASSERT(drawPacket.IsValid());

			if (drawItem.m_VariantBits != lastVariantBits)
			{
				const auto pipeline = GetOrCreatePSOForVariant(
					services, drawItem.m_VariantBits, context.GetDirectionalShadowSettings());
				graphicsContext->SetPipeline(pipeline);

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

			const auto& arguments = drawPacket.m_IndexedDraw;
			graphicsContext->DrawIndexed(arguments.m_IndexCount, arguments.m_InstanceCount,
				arguments.m_StartIndexLocation, arguments.m_BaseVertexLocation,
				arguments.m_StartInstanceLocation);
		}
	}

	RHIPipelineHandle RenderPassDirectionalShadowMap::GetOrCreatePSOForVariant(
		const RenderServices& services, uint64_t variantBits,
		const DirectionalShadowSettings& shadowSettings) noexcept
	{
		GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);
		auto* pipelineCache = services.m_PipelineResolver;
		GGLAB_ASSERT_NOT_NULL(pipelineCache);

		GraphicsPhysicalPipelineKey recipe = m_BaseRecipe;
		const RenderBucket bucket = RenderQueueBuilder::DecodeVariantBucket(variantBits);
		GGLAB_ASSERT(bucket == RenderBucket::Opaque || bucket == RenderBucket::AlphaTest);
		if (bucket == RenderBucket::AlphaTest)
		{
			recipe.m_PSId = m_AlphaTestPixelShader;
		}
		recipe.m_RasterizerPreset = GetRasterizerPresetFromVariantBits(variantBits);
		recipe.m_DepthBias = shadowSettings.m_RasterizerDepthBias;
		recipe.m_SlopeScaledDepthBias = shadowSettings.m_RasterizerSlopeScaledDepthBias;

		const size_t slotIndex = static_cast<size_t>(variantBits & RenderQueueBuilder::VariantMask);
		auto& slot = m_PipelineSlots[slotIndex];
		return pipelineCache->Resolve(slot, recipe, GetInfo());
	}

	RasterizerPreset RenderPassDirectionalShadowMap::GetRasterizerPresetFromVariantBits(
		uint64_t variantBits) const noexcept
	{
		const bool doubleSided = RenderQueueBuilder::DecodeVariantDoubleSided(variantBits);
		return doubleSided ? RasterizerPreset::TwoSided : RasterizerPreset::Default;
	}

}
