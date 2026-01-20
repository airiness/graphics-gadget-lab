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

				auto& targets = builder.GetBlackboard().GetOrCreate<RGFrameTargets>(MainViewName);
				data.m_BackBuffer = builder.Write(targets.m_BackBuffer, RGTextureUsage::RenderTarget);
				data.m_Depth = builder.Write(targets.m_Depth, RGTextureUsage::DepthStencil);

				data.m_RTVKey = targets.m_BackBufferRTVKey;
				data.m_Width = targets.m_Width;
				data.m_Height = targets.m_Height;
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
				auto* device = renderer->GetDevice();
				auto* descriptorManager = renderer->GetDescriptorManager();
				auto* passRegistry = renderer->GetRenderPassRecipeRegistry();
				auto* rootSignature = renderer->GetCommonRootSignature();
				auto* psoCache = renderer->GetPSOCache();

				const auto& cached = passRegistry->GetOrCreateGraphics(
					"RenderPassForwardPBR.main", m_KeyInputs,
					[&renderer](GraphicsPipelineDesc& outDesc, const GraphicsKeyInputs& input, ShaderManager* sm)
					{
						outDesc.m_RootSignatureId = input.m_RootSignatureId;
						outDesc.m_RootSignature = renderer->GetRootSignatureCache()->GetDX12RootSignature(input.m_RootSignatureId)->Get();
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

				auto* pso = psoCache->GetOrCreate(cached.m_Key, cached.m_Desc);

				commandList->SetDescriptorHeap(*descriptorManager->GetHeap(DX12DescriptorManager::HeapType::CbvSrvUav));
				commandList->SetGraphicsRootSignature(*rootSignature);
				commandList->SetPipelineState(*pso);

				commandList->SetViewport(0, 0, data.m_Width, data.m_Height);
				commandList->SetScissorRect(0, 0, data.m_Width, data.m_Height);
				commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				commandList->SetGraphicsConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::FrameCB),
					renderer->GetFrameConstantBuffer()->GetGPUVirtualAddress(contextPtr->m_BackBufferIndex));

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

				// Model Draw
				DrawModels(commandList, *contextPtr, *servicesPtr);

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
			m_KeyInputs.m_RootSignatureId = renderer->GetCommonRootSignatureId();
			m_KeyInputs.m_InputLayoutId = InputLayoutId::P3N3T2;
			m_KeyInputs.m_VSId = vsId;
			m_KeyInputs.m_PSId = psId;

			m_KeyInputs.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			m_KeyInputs.m_Formats.m_RtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			m_KeyInputs.m_Formats.m_RtvFormats.NumRenderTargets = 1;
			m_KeyInputs.m_Formats.m_DsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			m_KeyInputs.m_Formats.m_SampleCount = 1;
			m_KeyInputs.m_Formats.m_SampleQuality = 0;
			m_KeyInputs.m_RasterizerPreset = RasterizerPreset::Default;
			m_KeyInputs.m_BlendPreset = BlendPreset::Default;
			m_KeyInputs.m_DepthPreset = DepthPreset::Default;

			m_IsInitialized = true;
		}

		// For shader hot reload
		m_KeyInputs.m_VSGen = shaderManager->GetGeneration(m_KeyInputs.m_VSId);
		m_KeyInputs.m_PSGen = shaderManager->GetGeneration(m_KeyInputs.m_PSId);
	}


	void RenderPassForwardPBR::DrawModels(DX12CommandList* commandList,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		auto* assetManager = services.m_AssetManager;

		const auto& drawItems = context.m_RenderScene->m_DrawItems;
		for (const auto& drawItem : drawItems)
		{
			const Mesh* mesh = assetManager->GetMesh(drawItem.m_MeshId);
			if (!mesh || mesh->m_IndexCount == 0 || !mesh->m_IsUploaded)
			{
				continue;
			}

			D3D12_VERTEX_BUFFER_VIEW vbs[] = { mesh->m_VertexBufferView };
			commandList->SetVertexBuffers(0, vbs);
			commandList->SetIndexBuffer(mesh->m_IndexBufferView);

			commandList->Get()->SetGraphicsRoot32BitConstant(
				static_cast<uint32_t>(CommonRSRootParamIndex::ObjectCB),
				drawItem.m_ObjectOffset, 0);

			commandList->DrawIndexedInstanced(mesh->m_IndexCount);
		}

		// TODO: Sort and batch draw calls by PSO, Material and Texture.
	}
}