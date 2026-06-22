#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassDirectionalShadowMap.h"
#include "Graphics/Renderer.h"
#include "Graphics/ShaderManager.h"
#include "Graphics/AssetManager.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGShadowResources.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorManager.h"

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

		rg.AddPass<PassData>("RenderPassDirectionalShadowMap",
			[](RenderGraph::RGBuilder& builder, PassData& data)
			{
				auto& shadowRes = builder.GetBlackboard().Get<RGShadowResources>(ShadowResourcesName);
				data.m_ShadowMap = builder.Write(shadowRes.m_DirectionalShadowMap, RGTextureUsage::DepthStencil);
				data.m_ShadowMapSize = shadowRes.m_ShadowMapSize;

				const auto dsvDesc = MakeRGTexture2DViewDesc(RGFormat::D32Float);
				data.m_Dsv = builder.CreateView<RGTextureViewType::DSV>(data.m_ShadowMap, dsvDesc);
			},
			[this, &rg, contextPtr, servicesPtr](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandList = executeContext.GetGraphicsCommandList();
				GGLAB_ASSERT_NOT_NULL(commandList);

				auto* shadowMapTexture = rg.GetTexture(data.m_ShadowMap);
				GGLAB_ASSERT_NOT_NULL(shadowMapTexture);

				const auto dsv = executeContext.GetView(data.m_Dsv);

				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);

				auto* descriptorManager = renderer->GetDescriptorManager();
				GGLAB_ASSERT_NOT_NULL(descriptorManager);

				const DX12DescriptorHeap* descriptorHeaps[] = {
					descriptorManager->GetHeap(DX12DescriptorManager::HeapType::CbvSrvUav),
					descriptorManager->GetHeap(DX12DescriptorManager::HeapType::Sampler)
				};
				commandList->SetDescriptorHeaps(descriptorHeaps);
				commandList->SetGraphicsRootSignature(*renderer->GetCommonRootSignature());
				commandList->SetViewport(0, 0, data.m_ShadowMapSize, data.m_ShadowMapSize);
				commandList->SetScissorRect(0, 0, data.m_ShadowMapSize, data.m_ShadowMapSize);
				commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				commandList->ClearDepthStencil(dsv, 1.0f, 0);
				commandList->SetRenderTargets(std::span<DX12DescriptorView>{}, dsv);

				commandList->SetGraphicsConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					renderer->GetSceneConstantBuffer()->GetGPUVirtualAddress(contextPtr->m_BackBufferIndex));

				const auto& objectSB = renderer->GetObjectStructuredBuffer();
				commandList->Get()->SetGraphicsRootShaderResourceView(
					static_cast<uint32_t>(CommonRSRootParamIndex::ObjectSB),
					objectSB->GetBuffer()->GPUVirtualAddress());

				const auto& materialSB = renderer->GetMaterialStructuredBuffer();
				commandList->Get()->SetGraphicsRootShaderResourceView(
					static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB),
					materialSB->GetBuffer()->GPUVirtualAddress());

				const auto& viewSB = renderer->GetViewStructuredBuffer();
				commandList->Get()->SetGraphicsRootShaderResourceView(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					viewSB->GetBuffer()->GPUVirtualAddress());

				const DirectionalShadowPassParameters passParameters{
					.ViewIndex = static_cast<uint32_t>(utils::ToIndex(RenderViewID::DirectionalShadow)),
				};
				commandList->SetGraphicsRoot32BitConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
					passParameters);

				DrawRenderQueue(commandList, *contextPtr, *servicesPtr);
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

			m_BaseRecipe.m_RootSignatureId = renderer->GetCommonRootSignatureId();
			m_BaseRecipe.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
			m_BaseRecipe.m_VSId = vsId;
			m_BaseRecipe.m_PSId = psId;

			m_BaseRecipe.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			m_BaseRecipe.m_Formats.m_RtvFormats.NumRenderTargets = 0;
			m_BaseRecipe.m_Formats.m_DsvFormat = DXGI_FORMAT_D32_FLOAT;
			m_BaseRecipe.m_Formats.m_SampleCount = 1;
			m_BaseRecipe.m_Formats.m_SampleQuality = 0;

			m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
			m_BaseRecipe.m_BlendPreset = BlendPreset::ColorWriteDisable;
			m_BaseRecipe.m_DepthPreset = DepthPreset::Default;

			m_IsInitialized = true;
		}

	}

	void RenderPassDirectionalShadowMap::DrawRenderQueue(DX12CommandList* commandList,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		const auto& renderQueue = context.GetRenderQueue(RenderViewID::DirectionalShadow);
		if (renderQueue.m_DrawItems.empty())
		{
			return;
		}

		const auto ranges = renderQueue.m_BucketDrawRanges;
		DrawRange(commandList, context, services, renderQueue, ranges[utils::ToIndex(RenderBucket::Opaque)]);
		DrawRange(commandList, context, services, renderQueue, ranges[utils::ToIndex(RenderBucket::AlphaTest)]);
	}

	void RenderPassDirectionalShadowMap::DrawRange(DX12CommandList* commandList,
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

			if (drawItem.m_VariantBits != lastVariantBits)
			{
				auto* pso = GetOrCreatePSOForVariant(
					*renderer,
					drawItem.m_VariantBits,
					context.GetDirectionalShadowSettings());
				commandList->SetPipelineState(*pso);

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
				D3D12_VERTEX_BUFFER_VIEW vbs[] = { mesh->m_VertexBufferView };
				commandList->SetVertexBuffers(0, vbs);
				commandList->SetIndexBuffer(mesh->m_IndexBufferView);
				lastMeshId = drawItem.m_MeshId;
			}

			const DrawParameters drawParameters{
				.ObjectOffset = drawItem.m_ObjectOffset,
			};
			commandList->SetGraphicsRoot32BitConstants(
				static_cast<uint32_t>(CommonRSRootParamIndex::DrawConstants),
				drawParameters);

			commandList->DrawIndexedInstanced(mesh->m_IndexCount);
		}
	}

	DX12PipelineState* RenderPassDirectionalShadowMap::GetOrCreatePSOForVariant(
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
		return pipelineCache->Resolve(slot, recipe);
	}

	RasterizerPreset RenderPassDirectionalShadowMap::GetRasterizerPresetFromVariantBits(uint64_t variantBits) const noexcept
	{
		const bool doubleSided = RenderQueueBuilder::DecodeVariantDoubleSided(variantBits);
		return doubleSided ? RasterizerPreset::TwoSided : RasterizerPreset::Default;
	}

}
