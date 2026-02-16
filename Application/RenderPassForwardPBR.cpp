#include "Precompiled.h"
#include "RenderPassForwardPBR.h"
#include "Renderer.h"
#include "RenderScene.h"
#include "RenderGraph.h"
#include "RGFrameTargets.h"
#include "DX12SwapChain.h"
#include "DX12DescriptorHeap.h"
#include "DX12DescriptorManager.h"
#include "DX12CommandList.h"
#include "InputLayoutLibrary.h"
#include "AssetManager.h"

namespace gglab
{
	void RenderPassForwardPBR::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		EnsureInitialized(services);

		struct ForwardPBRData
		{
			RGTextureId m_BackBuffer{};
			RGTextureId m_Depth{};

			ViewKey m_RTVKey{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};

		auto* contextPtr = &context;
		auto* servicesPtr = &services;

		rg.AddPass<ForwardPBRData>("RenderPassForwardPBR",
			[](RenderGraph::RGBuilder& builder, ForwardPBRData& data)
			{
				builder.SideEffect();

				auto& targetsTable = builder.GetBlackboard().GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& mainTargets = targetsTable.GetViewTargets(RenderViewID::Main);
				data.m_BackBuffer = builder.Write(mainTargets.m_Color, RGTextureUsage::RenderTarget);
				data.m_Depth = builder.Write(mainTargets.m_Depth, RGTextureUsage::DepthStencil);

				data.m_RTVKey = mainTargets.m_BackBufferRTVKey;
				data.m_Width = mainTargets.m_Width;
				data.m_Height = mainTargets.m_Height;
			},
			[this, &rg, contextPtr, servicesPtr](DX12CommandList* commandList, ForwardPBRData& data)
			{
				auto* backTexture = rg.GetTexture(data.m_BackBuffer);
				auto* depthTexture = rg.GetTexture(data.m_Depth);

				GGLAB_ASSERT(backTexture && depthTexture);

				auto* viewCache = rg.GetViewCache();

				const auto& rtv = viewCache->GetOrCreate(data.m_RTVKey, backTexture);

				const ResourceIndex depthIndex = rg.GetResourceIndex(data.m_Depth);
				const ViewKey dsvKey = DX12ViewCache::BuildKey<ViewType::DSV>(
					depthIndex, depthTexture);

				const auto& dsv = viewCache->GetOrCreate(dsvKey, depthTexture);
				commandList->ClearDepthStencil(dsv, 1.0f, 0);
				commandList->SetRenderTarget(rtv, dsv);

				auto* renderer = servicesPtr->m_Renderer;
				auto* descriptorManager = renderer->GetDescriptorManager();
				auto* rootSignature = renderer->GetCommonRootSignature();

				// Global bindings
				commandList->SetDescriptorHeap(*descriptorManager->GetHeap(DX12DescriptorManager::HeapType::CbvSrvUav));
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
				
				// Set view index constant	
				commandList->Get()->SetGraphicsRoot32BitConstant(
					static_cast<uint32_t>(CommonRSRootParamIndex::ObjectCB),
					static_cast<uint32_t>(utils::ToIndex(RenderViewID::Main)),
					1);

				DrawRenderQueue(commandList, *contextPtr, *servicesPtr);
			});
	}

	void RenderPassForwardPBR::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		auto* shaderManager = services.m_ShaderManager;

		if (!m_IsInitialized)
		{
			// Shader
			ShaderDesc sd{};
			sd.m_SourcePath = L"Assets/Shaders/Passes/PassForwardPBR.hlsl";
			sd.m_Stage = ShaderStage::Vertex;
			sd.m_Entry = L"VSMain";
			const auto vsId = shaderManager->LoadShader(sd);
			sd.m_Stage = ShaderStage::Pixel;
			sd.m_Entry = L"PSMain";
			const auto psId = shaderManager->LoadShader(sd);

			// KeyInputs
			m_BaseInputs.m_RootSignatureId = renderer->GetCommonRootSignatureId();
			m_BaseInputs.m_InputLayoutId = InputLayoutId::P3N3T2;
			m_BaseInputs.m_VSId = vsId;
			m_BaseInputs.m_PSId = psId;

			m_BaseInputs.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			m_BaseInputs.m_Formats.m_RtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			m_BaseInputs.m_Formats.m_RtvFormats.NumRenderTargets = 1;
			m_BaseInputs.m_Formats.m_DsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			m_BaseInputs.m_Formats.m_SampleCount = 1;
			m_BaseInputs.m_Formats.m_SampleQuality = 0;
			m_BaseInputs.m_RasterizerPreset = RasterizerPreset::Default;
			m_BaseInputs.m_BlendPreset = BlendPreset::Default;
			m_BaseInputs.m_DepthPreset = DepthPreset::Default;

			m_IsInitialized = true;
		}

		// For shader hot reload
		m_BaseInputs.m_VSGen = shaderManager->GetGeneration(m_BaseInputs.m_VSId);
		m_BaseInputs.m_PSGen = shaderManager->GetGeneration(m_BaseInputs.m_PSId);
	}

	void RenderPassForwardPBR::DrawRenderQueue(DX12CommandList* commandList,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		const auto& renderQueue = context.m_RenderQueue;
		if (renderQueue.m_DrawItems.empty())
		{
			return;
		}
		const auto ranges = renderQueue.m_BucketDrawRanges;

		DrawItemsRange opaqueRange = ranges[utils::ToIndex(RenderBucket::Opaque)];
		DrawItemsRange alphaTestRange = ranges[utils::ToIndex(RenderBucket::AlphaTest)];
		DrawItemsRange transparentRange = ranges[utils::ToIndex(RenderBucket::Transparent)];

		DrawRange(commandList, context, services, opaqueRange);
		DrawRange(commandList, context, services, alphaTestRange);
		DrawRange(commandList, context, services, transparentRange);
	}

	void RenderPassForwardPBR::DrawRange(DX12CommandList* commandList,
		const RenderFrameContext& context,
		const RenderServices& services,
		const DrawItemsRange& range) noexcept
	{
		if (range.m_Count == 0)
		{
			return;
		}

		auto* renderer = services.m_Renderer;
		auto* assetManager = services.m_AssetManager;

		const auto& drawItems = context.m_RenderQueue.m_DrawItems;
		const auto& renderQueue = context.m_RenderQueue;

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
			commandList->Get()->SetGraphicsRoot32BitConstant(
				static_cast<uint32_t>(CommonRSRootParamIndex::ObjectCB),
				drawItem.m_ObjectOffset, 0);

			commandList->DrawIndexedInstanced(mesh->m_IndexCount);
		}
	}

	DX12PipelineState* RenderPassForwardPBR::GetOrCreatePSOForVariant(
		const Renderer& renderer, uint64_t variantBits) noexcept
	{
		auto* passRegistry = renderer.GetRenderPassRecipeRegistry();
		auto* psoCache = renderer.GetPSOCache();

		GraphicsKeyInputs inputs = m_BaseInputs;
		inputs.m_VariantBits = variantBits;

		auto [rasterizerPreset, depthPreset, blendPreset] = GetPresetsFromVariantBits(variantBits);
		inputs.m_RasterizerPreset = rasterizerPreset;
		inputs.m_DepthPreset = depthPreset;
		inputs.m_BlendPreset = blendPreset;

		const auto psoPassId = MakeVariantPSOPassId("RenderPassForwardPBR.variant", variantBits);

		const auto& cached = passRegistry->GetOrCreateGraphics(
			psoPassId, inputs,
			[&renderer](GraphicsPipelineDesc& outDesc, const GraphicsKeyInputs& input, ShaderManager* sm)
			{
				outDesc.m_RootSignatureId = input.m_RootSignatureId;
				outDesc.m_RootSignature = renderer.GetRootSignatureCache()->GetDX12RootSignature(input.m_RootSignatureId)->Get();
				outDesc.m_InputLayoutId = input.m_InputLayoutId;
				outDesc.m_InputLayoutDesc = InputLayoutLibrary::Get(InputLayoutId::P3N3T2);
				outDesc.m_VertexShader = sm->GetBytecode(input.m_VSId);
				outDesc.m_PixelShader = sm->GetBytecode(input.m_PSId);
				outDesc.m_Topology = input.m_Topology;
				outDesc.m_SampleMask = input.m_SampleMask;
				outDesc.m_Formats = input.m_Formats;
				outDesc.m_RasterizerDesc = ApplyRasterizerPreset(input.m_RasterizerPreset);
				outDesc.m_BlendDesc = ApplyBlendPreset(input.m_BlendPreset);
				outDesc.m_DepthDesc = ApplyDepthPreset(input.m_DepthPreset);
			});


		return psoCache->GetOrCreate(cached.m_Key, cached.m_Desc);
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

	std::string RenderPassForwardPBR::MakeVariantPSOPassId(
		std::string_view baseName, uint64_t variantBits) const noexcept
	{
		return std::format("{}.{:016X}", baseName, variantBits);
	}
}