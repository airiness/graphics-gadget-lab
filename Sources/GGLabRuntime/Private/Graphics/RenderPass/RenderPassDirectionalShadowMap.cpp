#include "Graphics/RenderPass/RenderPassDirectionalShadowMap.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicConstantBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicStructuredBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/ShadowGraphResources.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"

#include <algorithm>
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
			const RenderQueue* m_RenderQueue = nullptr;
			const DepthCoverageRasterDomain* m_RasterDomain = nullptr;
		};

	}

	void RenderPassDirectionalShadowMap::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		auto* contextPtr = &context;
		GGLAB_ASSERT_NOT_NULL(contextPtr);
		const DirectionalShadowFramePlan& cascades = context.GetDirectionalShadowFramePlan();
		GGLAB_ASSERT(cascades.m_Cascades.size() <= MaxDirectionalShadowCascades);
		if (cascades.m_Cascades.empty())
		{
			return;
		}
		EnsureInitialized(services);
		static constexpr const char* passNames[] = {
			"Shadow.Directional.Cascade0", "Shadow.Directional.Cascade1",
			"Shadow.Directional.Cascade2", "Shadow.Directional.Cascade3",
		};
		const uint32_t layerCount = static_cast<uint32_t>(cascades.m_Cascades.size());
		for (uint32_t cascadeIndex = 0; cascadeIndex < layerCount; ++cascadeIndex)
		{
			const DirectionalShadowCascade* cascade = cascades.TryGetCascade(cascadeIndex);
			const uint32_t viewIndex = cascade ? cascades.GetViewIndex(cascadeIndex) : 0u;

			rg.AddPass<PassData>(
				passNames[cascadeIndex],
				[contextPtr, cascade, viewIndex, cascadeIndex](
					RenderGraph::RGBuilder& builder, PassData& data)
				{
					auto& shadowRes =
						builder.GetBlackboard().Get<RGShadowResources>(ShadowResourcesName);
					const auto dsvDesc = MakeRHITexture2DArrayViewDesc(
						RHIFormat::D32Float, 0, cascadeIndex, 1, RHITextureAspect::Depth);
					// R32 has one plane. Leave the dependency aspect mask open until
					// Compile infers depth usage for the typeless resource; the DSV is explicitly Depth.
					builder.WriteInPlace(shadowRes.m_DirectionalShadowMap,
						RGTextureAccess::DepthStencilWrite, RHISubresourceRange{
							.m_MipCount = 1, .m_BaseArraySlice = cascadeIndex, .m_ArraySliceCount = 1 });
					data.m_ShadowMap = shadowRes.m_DirectionalShadowMap;
					if (cascade)
					{
						data.m_RenderQueue = std::addressof(cascade->m_RenderQueue);
						data.m_RasterDomain =
							std::addressof(cascade->m_RenderQueue.m_CoverageRasterDomain);
					}
					if (data.m_RenderQueue && !data.m_RenderQueue->m_DrawItems.empty())
					{
						const auto& shadowDesc = builder.GetTextureDesc(data.m_ShadowMap);
						GGLAB_ASSERT_MSG(data.m_RasterDomain->m_ViewBindingId == viewIndex &&
							data.m_RasterDomain->m_CurrentViewSource.m_ElementIndex ==
								contextPtr->m_RenderScene.m_ViewBaseIndex + viewIndex,
							"Shadow coverage and shader binding must address the same uploaded cascade view.");
						GGLAB_ASSERT_MSG(
							data.m_RasterDomain->IsValid() &&
							IsDepthCoverageTargetExtentCompatible(*data.m_RasterDomain, shadowDesc),
							"Shadow-map extent must match its coverage raster domain.");
					}

					data.m_Dsv =
						builder.CreateView<RHITextureViewType::DepthStencil>(data.m_ShadowMap, dsvDesc);
				},
				[this, contextPtr, services, viewIndex](
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
					graphicsContext->ClearDepthAttachment(1.0f);

					// A frame without a cascade keeps this cleared write and submits nothing.
					if (!data.m_RenderQueue || data.m_RenderQueue->m_DrawItems.empty())
					{
						return;
					}
					const RenderQueue& renderQueue = *data.m_RenderQueue;
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
						renderQueue.m_DrawItems[firstDrawRange->m_Start].m_VariantBits));

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
						.ViewIndex = viewIndex,
					};
					graphicsContext->SetPushConstants(
						static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), passParameters);

					DrawRenderQueue(graphicsContext, services, renderQueue);
				});
		}
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
			m_BaseRecipe.m_DepthBias = 0;
			m_BaseRecipe.m_SlopeScaledDepthBias = 0.0f;
			m_BaseRecipe.m_BlendPreset = BlendPreset::ColorWriteDisable;
			m_BaseRecipe.m_DepthPreset = DepthPreset::StandardZWrite;

			m_IsInitialized = true;
		}
	}

	void RenderPassDirectionalShadowMap::DrawRenderQueue(RHIGraphicsCommandContext* graphicsContext,
		const RenderServices& services, const RenderQueue& renderQueue) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(graphicsContext);
		if (renderQueue.m_DrawItems.empty())
		{
			return;
		}

		const auto ranges = renderQueue.m_BucketDrawRanges;
		DrawRange(graphicsContext, services, renderQueue,
			ranges[utils::ToIndex(RenderBucket::Opaque)]);
		DrawRange(graphicsContext, services, renderQueue,
			ranges[utils::ToIndex(RenderBucket::AlphaTest)]);
	}

	void RenderPassDirectionalShadowMap::DrawRange(RHIGraphicsCommandContext* graphicsContext,
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

		for (uint32_t index = 0; index < range.m_Count; ++index)
		{
			const auto& drawItem = drawItems[range.m_Start + index];
			const auto& drawPacket = drawItem.m_CoverageDrawPacket;
			GGLAB_ASSERT(drawPacket.IsValid());

			if (drawItem.m_VariantBits != lastVariantBits)
			{
				const auto pipeline = GetOrCreatePSOForVariant(
					services, drawItem.m_VariantBits);
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
		const RenderServices& services, uint64_t variantBits) noexcept
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
