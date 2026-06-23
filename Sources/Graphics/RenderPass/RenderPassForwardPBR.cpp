#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassForwardPBR.h"
#include "Graphics/Renderer.h"
#include "Graphics/ShaderManager.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGFrameTargets.h"
#include "Graphics/RenderGraph/RGIBLResources.h"
#include "Graphics/RenderGraph/RGShadowResources.h"
#include "Graphics/RHI/DX12/DX12SwapChain.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"
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

		rg.AddPass<PassData>("RenderPassForwardPBR",
			[contextPtr, servicesPtr](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();

				auto& targetsTable = blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& mainTargets = targetsTable.GetViewTargets(RenderViewID::Main);
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);
				auto& shadowRes = blackboard.Get<RGShadowResources>(ShadowResourcesName);

				data.m_SceneColor = builder.Write(mainTargets.m_SceneColor, RGTextureUsage::RenderTarget);
				data.m_Depth = builder.Write(mainTargets.m_Depth, RGTextureUsage::DepthStencil);
				data.m_IrradianceCubemap = builder.Read(iblRes.m_IrradianceCubemap, RGTextureUsage::Sample);
				data.m_PrefilteredSpecularCubemap = builder.Read(iblRes.m_PrefilteredSpecularCubemap, RGTextureUsage::Sample);
				data.m_BrdfLut = builder.Read(iblRes.m_BrdfLut, RGTextureUsage::Sample);
				data.m_ShadowMap = builder.Read(shadowRes.m_DirectionalShadowMap, RGTextureUsage::Sample);

				data.m_Rtv = builder.CreateView<RGTextureViewType::RTV>(data.m_SceneColor);
				data.m_Dsv = builder.CreateView<RGTextureViewType::DSV>(data.m_Depth);

				const auto shadowSrvDesc = MakeRGTexture2DViewDesc(RGFormat::R32Float, 0, 1);
				data.m_ShadowSrv =
					builder.CreateView<RGTextureViewType::SRV>(data.m_ShadowMap, shadowSrvDesc);

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
			[this, &rg, contextPtr, servicesPtr](RGExecuteContext& executeContext, PassData& data)
			{
				auto* sceneColorTexture = rg.GetTexture(data.m_SceneColor);
				GGLAB_ASSERT_NOT_NULL(sceneColorTexture);

				auto* commandList = executeContext.GetGraphicsCommandList();

				const auto rtv = executeContext.GetView(data.m_Rtv);
				const auto dsv = executeContext.GetView(data.m_Dsv);
				commandList->ClearRenderTarget(rtv, *sceneColorTexture);
				commandList->ClearDepthStencil(dsv, 1.0f, 0);
				commandList->SetRenderTarget(rtv, dsv);

				const auto shadowSrv = executeContext.GetView(data.m_ShadowSrv);
				GGLAB_ASSERT_MSG(shadowSrv.m_Index != std::numeric_limits<uint32_t>::max(),
					"ForwardPBR shadow map SRV must expose a descriptor heap index.");

				auto* renderer = servicesPtr->m_Renderer;
				auto* descriptorManager = renderer->GetDescriptorManager();
				auto* rootSignature = renderer->GetCommonRootSignature();

				// Global bindings
				const DX12DescriptorHeap* descriptorHeaps[] = {
					descriptorManager->GetHeap(DX12DescriptorManager::HeapType::CbvSrvUav),
					descriptorManager->GetHeap(DX12DescriptorManager::HeapType::Sampler)
				};
				commandList->SetDescriptorHeaps(descriptorHeaps);
				commandList->SetGraphicsRootSignature(*rootSignature);

				commandList->SetViewport(0, 0, data.m_Width, data.m_Height);
				commandList->SetScissorRect(0, 0, data.m_Width, data.m_Height);
				commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				commandList->SetGraphicsConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					renderer->GetSceneConstantBuffer()->GetGPUVirtualAddress(contextPtr->m_BackBufferIndex));

				// Set object structured buffer
				const auto& objectSB = renderer->GetObjectStructuredBuffer();
				commandList->Get()->SetGraphicsRootShaderResourceView(
					static_cast<uint32_t>(CommonRSRootParamIndex::ObjectSB),
					objectSB->GetBuffer()->GPUVirtualAddress());

				// Set material structured buffer
				const auto& materialSB = renderer->GetMaterialStructuredBuffer();
				commandList->Get()->SetGraphicsRootShaderResourceView(
					static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB),
					materialSB->GetBuffer()->GPUVirtualAddress());

				// View structured buffer
				const auto& viewSB = renderer->GetViewStructuredBuffer();
				commandList->Get()->SetGraphicsRootShaderResourceView(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					viewSB->GetBuffer()->GPUVirtualAddress());

				const ForwardPBRPassParameters passParameters{
					.ViewIndex = static_cast<uint32_t>(utils::ToIndex(RenderViewID::Main)),
					.ShadowMapTextureIndex = shadowSrv.m_Index,
					.ShadowMapSamplerIndex = data.m_ShadowSamplerIndex,
					.ShadowMapSize = data.m_ShadowMapSize,
					.ShadowFlags = data.m_ShadowFlags,
					.ShadowReceiverDepthBias = data.m_ShadowReceiverDepthBias,
					.ShadowViewIndex = static_cast<uint32_t>(utils::ToIndex(RenderViewID::DirectionalShadow)),
				};
				commandList->SetGraphicsRoot32BitConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
					passParameters);

				DrawRenderQueue(commandList, *contextPtr, *servicesPtr);
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
			m_BaseRecipe.m_RootSignatureId = renderer->GetCommonRootSignatureId();
			m_BaseRecipe.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
			m_BaseRecipe.m_VSId = vsId;
			m_BaseRecipe.m_PSId = psId;

			m_BaseRecipe.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			m_BaseRecipe.m_Formats.m_RtvFormats.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
			m_BaseRecipe.m_Formats.m_RtvFormats.NumRenderTargets = 1;
			m_BaseRecipe.m_Formats.m_DsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			m_BaseRecipe.m_Formats.m_SampleCount = 1;
			m_BaseRecipe.m_Formats.m_SampleQuality = 0;
			m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
			m_BaseRecipe.m_BlendPreset = BlendPreset::Default;
			m_BaseRecipe.m_DepthPreset = DepthPreset::Default;

			m_IsInitialized = true;
		}

	}

	void RenderPassForwardPBR::DrawRenderQueue(DX12CommandList* commandList,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		const auto& renderQueue = context.GetRenderQueue(RenderViewID::Main);
		if (renderQueue.m_DrawItems.empty())
		{
			return;
		}
		const auto ranges = renderQueue.m_BucketDrawRanges;

		DrawItemsRange opaqueRange = ranges[utils::ToIndex(RenderBucket::Opaque)];
		DrawItemsRange alphaTestRange = ranges[utils::ToIndex(RenderBucket::AlphaTest)];
		DrawItemsRange transparentRange = ranges[utils::ToIndex(RenderBucket::Transparent)];

		DrawRange(commandList, context, services, renderQueue, opaqueRange);
		DrawRange(commandList, context, services, renderQueue, alphaTestRange);
		DrawRange(commandList, context, services, renderQueue, transparentRange);
	}

	void RenderPassForwardPBR::DrawRange(DX12CommandList* commandList,
		const RenderFrameContext& context,
		const RenderServices& services,
		const RenderQueue& renderQueue,
		const DrawItemsRange& range) noexcept
	{
		if (range.m_Count == 0)
		{
			return;
		}

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
				auto* pso = GetOrCreatePSOForVariant(*services.m_Renderer, drawItem.m_VariantBits);
				commandList->SetPipelineState(*pso);

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
				D3D12_VERTEX_BUFFER_VIEW vbs[] = { mesh->m_VertexBufferView };
				commandList->SetVertexBuffers(0, vbs);
				commandList->SetIndexBuffer(mesh->m_IndexBufferView);
				lastMeshId = drawItem.m_MeshId;
			}

			// Per draw item bindings
			const DrawParameters drawParameters{
				.ObjectOffset = drawItem.m_ObjectOffset,
			};
			commandList->SetGraphicsRoot32BitConstants(
				static_cast<uint32_t>(CommonRSRootParamIndex::DrawConstants),
				drawParameters);

			commandList->DrawIndexedInstanced(mesh->m_IndexCount);
		}
	}

	DX12PipelineState* RenderPassForwardPBR::GetOrCreatePSOForVariant(
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
		return pipelineCache->Resolve(slot, recipe);
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
