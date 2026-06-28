#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassDirectionalShadowMap.h"
#include "Graphics/Renderer.h"
#include "Graphics/ShaderManager.h"
#include "Graphics/AssetManager.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGShadowResources.h"
#include "Graphics/RHI/RHICommandContext.h"

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
			uint32_t m_ShadowMapSize = 0;
		};

	}

	void RenderPassDirectionalShadowMap::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		auto* contextPtr = &context;
		GGLAB_ASSERT_NOT_NULL(contextPtr);

		auto* servicesPtr = &services;
		GGLAB_ASSERT_NOT_NULL(servicesPtr);

		EnsureInitialized(services);

		rg.AddPass<PassData>(GetRenderGraphPassName(),
			[](RenderGraph::RGBuilder& builder, PassData& data)
			{
				auto& shadowRes = builder.GetBlackboard().Get<RGShadowResources>(ShadowResourcesName);
				data.m_ShadowMap = builder.Write(shadowRes.m_DirectionalShadowMap, RGTextureAccess::DepthStencilWrite);
				data.m_ShadowMapSize = shadowRes.m_ShadowMapSize;

				const auto dsvDesc = MakeRHITexture2DViewDesc(RHIFormat::D32Float);
				data.m_Dsv = builder.CreateView<RHITextureViewType::DepthStencil>(data.m_ShadowMap, dsvDesc);
			},
			[this, contextPtr, servicesPtr](RGExecuteContext& executeContext, PassData& data)
			{
				auto* graphicsContext = executeContext.GetGraphicsCommandContext();
				GGLAB_ASSERT_NOT_NULL(graphicsContext);
				const auto dsv = executeContext.GetViewHandle(data.m_Dsv);

				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);
				graphicsContext->ClearDepthStencil(dsv, 1.0f, 0);
				graphicsContext->SetRenderTargets({}, dsv);

				const auto& renderQueue = contextPtr->GetRenderQueue(RenderViewID::DirectionalShadow);
				if (renderQueue.m_DrawItems.empty())
				{
					return;
				}
				graphicsContext->SetPipeline(GetOrCreatePSOForVariant(
					*renderer,
					renderQueue.m_DrawItems.front().m_VariantBits,
					contextPtr->GetDirectionalShadowSettings()));
				graphicsContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_ShadowMapSize), static_cast<float>(data.m_ShadowMapSize) });
				graphicsContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_ShadowMapSize), static_cast<int32_t>(data.m_ShadowMapSize) });
				graphicsContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

				const auto* sceneBuffer = renderer->GetSceneConstantBuffer();
				graphicsContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					sceneBuffer->GetBufferHandle(),
					contextPtr->m_RenderScene.m_SceneConstantBufferOffset);

				const auto& objectSB = renderer->GetObjectStructuredBuffer();
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ObjectSB),
					objectSB->GetBufferHandle(contextPtr->m_BackBufferIndex));

				const auto& materialSB = renderer->GetMaterialStructuredBuffer();
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB),
					materialSB->GetBufferHandle(contextPtr->m_BackBufferIndex));

				const auto& viewSB = renderer->GetViewStructuredBuffer();
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					viewSB->GetBufferHandle());

				const DirectionalShadowPassParameters passParameters{
					.ViewIndex = static_cast<uint32_t>(utils::ToIndex(RenderViewID::DirectionalShadow)),
				};
				graphicsContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
					passParameters);

				DrawRenderQueue(graphicsContext, *contextPtr, *servicesPtr);
			});
	}

	void RenderPassDirectionalShadowMap::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassDirectionalShadowMap.hlsl";
			shaderDesc.m_Stage = ShaderStage::Vertex;
			shaderDesc.m_Entry = L"VSMain";
			const auto vsId = shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSMain";
			const auto psId = shaderManager->LoadShader(shaderDesc);

			m_BaseRecipe.m_BindingLayout = renderer->GetCommonBindingLayout();
			m_BaseRecipe.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
			m_BaseRecipe.m_VSId = vsId;
			m_BaseRecipe.m_PSId = psId;

			m_BaseRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
			m_BaseRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
			m_BaseRecipe.m_Formats.m_RenderTargetCount = 0;
			m_BaseRecipe.m_Formats.m_DepthStencilFormat = RHIFormat::D32Float;
			m_BaseRecipe.m_Formats.m_SampleCount = 1;
			m_BaseRecipe.m_Formats.m_SampleQuality = 0;

			m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
			m_BaseRecipe.m_BlendPreset = BlendPreset::ColorWriteDisable;
			m_BaseRecipe.m_DepthPreset = DepthPreset::Default;

			m_IsInitialized = true;
		}

	}

	void RenderPassDirectionalShadowMap::DrawRenderQueue(RHIGraphicsCommandContext* graphicsContext,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(graphicsContext);
		const auto& renderQueue = context.GetRenderQueue(RenderViewID::DirectionalShadow);
		if (renderQueue.m_DrawItems.empty())
		{
			return;
		}

		const auto ranges = renderQueue.m_BucketDrawRanges;
		DrawRange(graphicsContext, context, services, renderQueue, ranges[utils::ToIndex(RenderBucket::Opaque)]);
		DrawRange(graphicsContext, context, services, renderQueue, ranges[utils::ToIndex(RenderBucket::AlphaTest)]);
	}

	void RenderPassDirectionalShadowMap::DrawRange(RHIGraphicsCommandContext* graphicsContext,
		const RenderFrameContext& context,
		const RenderServices& services,
		const RenderQueue& renderQueue,
		const DrawItemsRange& range) noexcept
	{
		if (range.m_Count == 0)
		{
			return;
		}
		GGLAB_ASSERT_NOT_NULL(graphicsContext);

		auto* renderer = services.m_Renderer;
		auto* assetManager = services.m_AssetManager;

		const auto& drawItems = renderQueue.m_DrawItems;

		uint64_t lastVariantBits = std::numeric_limits<uint64_t>::max();
		MeshID lastMeshId{};

		for (uint32_t index = 0; index < range.m_Count; ++index)
		{
			const auto& drawItem = drawItems[range.m_Start + index];

			if (drawItem.m_VariantBits != lastVariantBits)
			{
				const auto pipeline = GetOrCreatePSOForVariant(
					*renderer,
					drawItem.m_VariantBits,
					context.GetDirectionalShadowSettings());
				graphicsContext->SetPipeline(pipeline);

				lastVariantBits = drawItem.m_VariantBits;
				lastMeshId = {};
			}

			const Mesh* mesh = assetManager->GetMesh(drawItem.m_MeshId);
			if (!mesh || mesh->m_IndexCount == 0 || !mesh->m_IsUploaded)
			{
				GGLAB_LOG_GRAPHICS_WARN("RenderPassDirectionalShadowMap: Mesh is invalid, skip draw item.");
				continue;
			}

			if (drawItem.m_MeshId != lastMeshId)
			{
				graphicsContext->SetVertexBuffers(
					0,
					std::span<const RHIVertexBufferBinding>(&mesh->m_VertexBufferBinding, 1));
				graphicsContext->SetIndexBuffer(mesh->m_IndexBufferBinding);
				lastMeshId = drawItem.m_MeshId;
			}

			const DrawParameters drawParameters{
				.ObjectOffset = drawItem.m_ObjectOffset,
			};
			graphicsContext->SetPushConstants(
				static_cast<uint32_t>(CommonRSRootParamIndex::DrawConstants),
				drawParameters);

			graphicsContext->DrawIndexed(mesh->m_IndexCount);
		}
	}

	RHIPipelineHandle RenderPassDirectionalShadowMap::GetOrCreatePSOForVariant(
		const Renderer& renderer,
		uint64_t variantBits,
		const DirectionalShadowSettings& shadowSettings) noexcept
	{
		GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);

		GraphicsPipelineRecipe recipe = m_BaseRecipe;
		recipe.m_RasterizerPreset = GetRasterizerPresetFromVariantBits(variantBits);
		recipe.m_DepthBias = shadowSettings.m_RasterizerDepthBias;
		recipe.m_SlopeScaledDepthBias = shadowSettings.m_RasterizerSlopeScaledDepthBias;

		const size_t slotIndex =
			static_cast<size_t>(variantBits & RenderQueueBuilder::VariantMask);
		auto& slot = m_PipelineSlots[slotIndex];
		return pipelineCache->Resolve(slot, recipe, GetInfo());
	}

	RasterizerPreset RenderPassDirectionalShadowMap::GetRasterizerPresetFromVariantBits(uint64_t variantBits) const noexcept
	{
		const bool doubleSided = RenderQueueBuilder::DecodeVariantDoubleSided(variantBits);
		return doubleSided ? RasterizerPreset::TwoSided : RasterizerPreset::Default;
	}

}
