#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassForwardPBR.h"
#include "Graphics/Renderer.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGFrameTargets.h"
#include "Graphics/RenderGraph/RGIBLResources.h"
#include "Graphics/RenderGraph/RGShadowResources.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/AssetManager.h"
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

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
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
		auto* contextPtr = &context;
		GGLAB_ASSERT_NOT_NULL(contextPtr);

		auto* servicesPtr = &services;
		GGLAB_ASSERT_NOT_NULL(servicesPtr);

		EnsureInitialized(services);

		rg.AddPass<PassData>(GetRenderGraphPassName(),
			[contextPtr, servicesPtr](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();

				auto& targetsTable = blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& mainTargets = targetsTable.GetViewTargets(RenderViewID::Main);
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);
				auto& shadowRes = blackboard.Get<RGShadowResources>(ShadowResourcesName);

				data.m_SceneColor = builder.Write(mainTargets.m_SceneColor, RGTextureAccess::RenderTarget);
				data.m_Depth = builder.Write(mainTargets.m_Depth, RGTextureAccess::DepthStencilWrite);
				data.m_IrradianceCubemap = builder.Read(iblRes.m_IrradianceCubemap, RGTextureAccess::Sample);
				data.m_PrefilteredSpecularCubemap = builder.Read(iblRes.m_PrefilteredSpecularCubemap, RGTextureAccess::Sample);
				data.m_BrdfLut = builder.Read(iblRes.m_BrdfLut, RGTextureAccess::Sample);
				data.m_ShadowMap = builder.Read(shadowRes.m_DirectionalShadowMap, RGTextureAccess::Sample);

				data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(data.m_SceneColor);
				data.m_Dsv = builder.CreateView<RHITextureViewType::DepthStencil>(data.m_Depth);

				const auto shadowSrvDesc = MakeRHITexture2DViewDesc(RHIFormat::R32Float, 0, 1);
				data.m_ShadowSrv =
					builder.CreateView<RHITextureViewType::ShaderResource>(data.m_ShadowMap, shadowSrvDesc);

				data.m_Width = mainTargets.m_Width;
				data.m_Height = mainTargets.m_Height;
				data.m_ShadowMapSize = shadowRes.m_ShadowMapSize;

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
			[this, contextPtr, servicesPtr](RGExecuteContext& executeContext, PassData& data)
			{
				auto* graphicsContext = executeContext.GetGraphicsCommandContext();
				GGLAB_ASSERT_NOT_NULL(graphicsContext);

				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				const auto dsv = executeContext.GetViewHandle(data.m_Dsv);
				graphicsContext->ClearColor(rtv, { 0.0f, 0.0f, 0.0f, 1.0f });
				graphicsContext->ClearDepthStencil(dsv, 1.0f, 0);
				graphicsContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&rtv, 1), dsv);

				const auto shadowSrv = executeContext.GetViewDescriptor(data.m_ShadowSrv);
				GGLAB_ASSERT_MSG(shadowSrv.IsValid(),
					"ForwardPBR shadow map SRV must expose a descriptor heap index.");

				auto* renderer = servicesPtr->m_Renderer;
				const auto& renderQueue = contextPtr->GetRenderQueue(RenderViewID::Main);
				if (renderQueue.m_DrawItems.empty())
				{
					return;
				}
				graphicsContext->SetPipeline(GetOrCreatePSOForVariant(
					*renderer,
					renderQueue.m_DrawItems.front().m_VariantBits));
				graphicsContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width), static_cast<float>(data.m_Height) });
				graphicsContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width), static_cast<int32_t>(data.m_Height) });
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

				const ForwardPBRPassParameters passParameters{
					.ViewIndex = static_cast<uint32_t>(utils::ToIndex(RenderViewID::Main)),
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

				DrawRenderQueue(graphicsContext, *contextPtr, *servicesPtr);
			});
	}

	void RenderPassForwardPBR::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			// Shader
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassForwardPBR.hlsl";
			shaderDesc.m_Stage = ShaderStage::Vertex;
			shaderDesc.m_Entry = L"VSMain";
			const auto vsId = shaderManager->LoadShader(shaderDesc);
			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSMain";
			const auto psId = shaderManager->LoadShader(shaderDesc);

			// Pipeline recipe
			m_BaseRecipe.m_BindingLayout = renderer->GetCommonBindingLayout();
			m_BaseRecipe.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
			m_BaseRecipe.m_VSId = vsId;
			m_BaseRecipe.m_PSId = psId;

			m_BaseRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
			m_BaseRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
			m_BaseRecipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R16G16B16A16Float;
			m_BaseRecipe.m_Formats.m_RenderTargetCount = 1;
			m_BaseRecipe.m_Formats.m_DepthStencilFormat = RHIFormat::D24UnormS8Uint;
			m_BaseRecipe.m_Formats.m_SampleCount = 1;
			m_BaseRecipe.m_Formats.m_SampleQuality = 0;
			m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
			m_BaseRecipe.m_BlendPreset = BlendPreset::Default;
			m_BaseRecipe.m_DepthPreset = DepthPreset::Default;

			m_IsInitialized = true;
		}

	}

	void RenderPassForwardPBR::DrawRenderQueue(RHIGraphicsCommandContext* graphicsContext,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(graphicsContext);
		const auto& renderQueue = context.GetRenderQueue(RenderViewID::Main);
		if (renderQueue.m_DrawItems.empty())
		{
			return;
		}
		const auto ranges = renderQueue.m_BucketDrawRanges;

		DrawItemsRange opaqueRange = ranges[utils::ToIndex(RenderBucket::Opaque)];
		DrawItemsRange alphaTestRange = ranges[utils::ToIndex(RenderBucket::AlphaTest)];
		DrawItemsRange transparentRange = ranges[utils::ToIndex(RenderBucket::Transparent)];

		DrawRange(graphicsContext, context, services, renderQueue, opaqueRange);
		DrawRange(graphicsContext, context, services, renderQueue, alphaTestRange);
		DrawRange(graphicsContext, context, services, renderQueue, transparentRange);
	}

	void RenderPassForwardPBR::DrawRange(RHIGraphicsCommandContext* graphicsContext,
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

			// Set PSO
			if (drawItem.m_VariantBits != lastVariantBits)
			{
				graphicsContext->SetPipeline(
					GetOrCreatePSOForVariant(*services.m_Renderer, drawItem.m_VariantBits));

				lastVariantBits = drawItem.m_VariantBits;
				lastMeshId = {}; // Because PSO changed
			}

			// Set Mesh
			const Mesh* mesh = assetManager->GetMesh(drawItem.m_MeshId);
			if (!mesh || mesh->m_IndexCount == 0 || !mesh->m_IsUploaded)
			{
				GGLAB_LOG_GRAPHICS_WARN("RenderPassForwardPBR: Mesh is invalid, skip draw item.");
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

			// Per draw item bindings
			const DrawParameters drawParameters{
				.ObjectOffset = drawItem.m_ObjectOffset,
			};
			graphicsContext->SetPushConstants(
				static_cast<uint32_t>(CommonRSRootParamIndex::DrawConstants),
				drawParameters);

			graphicsContext->DrawIndexed(mesh->m_IndexCount);
		}
	}

	RHIPipelineHandle RenderPassForwardPBR::GetOrCreatePSOForVariant(
		const Renderer& renderer, uint64_t variantBits) noexcept
	{
		GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);

		GraphicsPipelineRecipe recipe = m_BaseRecipe;

		auto [rasterizerPreset, depthPreset, blendPreset] = GetPresetsFromVariantBits(variantBits);
		recipe.m_RasterizerPreset = rasterizerPreset;
		recipe.m_DepthPreset = depthPreset;
		recipe.m_BlendPreset = blendPreset;

		const size_t slotIndex =
			static_cast<size_t>(variantBits & RenderQueueBuilder::VariantMask);
		auto& slot = m_PipelineSlots[slotIndex];
		return pipelineCache->Resolve(slot, recipe, GetInfo());
	}

	std::tuple<RasterizerPreset, DepthPreset, BlendPreset>
		RenderPassForwardPBR::GetPresetsFromVariantBits(uint64_t variantBits) const noexcept
	{
		const bool doubleSided = RenderQueueBuilder::DecodeVariantDoubleSided(variantBits);
		const auto renderBucket = RenderQueueBuilder::DecodeVariantBucket(variantBits);

		RasterizerPreset rasterizerPreset = doubleSided ?
			RasterizerPreset::TwoSided :
			RasterizerPreset::Default;
		BlendPreset blendPreset = BlendPreset::Default;
		DepthPreset depthPreset = DepthPreset::Default;

		if (renderBucket == RenderBucket::Transparent)
		{
			blendPreset = BlendPreset::AlphaBlend;
			depthPreset = DepthPreset::DepthReadOnly;
		}

		return { rasterizerPreset, depthPreset, blendPreset };
	}

}
