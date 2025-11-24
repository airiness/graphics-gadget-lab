#include "Precompiled.h"
#include "RenderPassForwardPBR.h"
#include "Application.h"
#include "Renderer.h"
#include "RenderGraph.h"
#include "DX12SwapChain.h"
#include "DX12CommandList.h"
#include "InputLayoutLibrary.h"
#include "AssetManager.h"
#include "Components.h"

namespace gglab
{
	RenderPassForwardPBR::RenderPassForwardPBR() noexcept
	{
		auto* renderer = Application::GetInstance()->GetRenderer();
		auto* shaderManager = Application::GetInstance()->GetShaderManager();

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
		m_KeyInputs.m_VSGen = shaderManager->GetGeneration(vsId);
		m_KeyInputs.m_PSGen = shaderManager->GetGeneration(psId);
		m_KeyInputs.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		m_KeyInputs.m_Formats.m_RtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		m_KeyInputs.m_Formats.m_RtvFormats.NumRenderTargets = 1;
		m_KeyInputs.m_Formats.m_DsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		m_KeyInputs.m_Formats.m_SampleCount = 1;
		m_KeyInputs.m_Formats.m_SampleQuality = 0;
		m_KeyInputs.m_RasterizerPreset = RasterizerPreset::Default;
		m_KeyInputs.m_BlendPreset = BlendPreset::Default;
		m_KeyInputs.m_DepthPreset = DepthPreset::Default;
	}

	void RenderPassForwardPBR::AddPass(RenderGraph& rg) noexcept
	{
		struct ForwardPBRData
		{
			RGTextureId m_Depth;
		};

		rg.AddPass<ForwardPBRData>("RenderPassForwardPBR",
			[this](RenderGraph::RGBuilder& builder, ForwardPBRData& data)
			{
				builder.SideEffect();
				auto* renderer = Application::GetInstance()->GetRenderer();
				auto* swapChain = renderer->GetDevice()->GetSwapChain();

				RGTextureDesc dsDesc = {};
				dsDesc.m_Width = swapChain->GetBufferWidth();
				dsDesc.m_Height = swapChain->GetBufferHeight();
				dsDesc.m_Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
				dsDesc.m_Usage = RGTextureUsage::DepthStencil;

				data.m_Depth = builder.CreateTexture("Depth", dsDesc);
				builder.Write<RGTextureResource>(data.m_Depth, RGTextureUsage::DepthStencil);
			},
			[this, &rg](DX12CommandList* commandList, ForwardPBRData& data)
			{
				auto* renderer = Application::GetInstance()->GetRenderer();
				auto* device = renderer->GetDevice();
				auto* passRegistry = renderer->GetRenderPassRecipeRegistry();
				auto* rootSignature = renderer->GetCommonRootSignature();
				auto* psoCache = renderer->GetPSOCache();
				auto* swapChain = device->GetSwapChain();

				const auto width = swapChain->GetBufferWidth();
				const auto height = swapChain->GetBufferHeight();

				const auto& cached = passRegistry->GetOrCreateGraphics(
					"RenderPassForwardPBR.main", m_KeyInputs,
					[](GraphicsPipelineDesc& outDesc, const GraphicsKeyInputs& input, ShaderManager* sm)
					{
						auto* renderer = Application::GetInstance()->GetRenderer();
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

				commandList->SetDescriptorHeap(*device->GetCbvSrvUavDescriptorAllocator()->GetHeap());
				commandList->SetGraphicsRootSignature(*rootSignature);
				commandList->SetPipelineState(*pso);

				commandList->SetViewport(0, 0, width, height);
				commandList->SetScissorRect(0, 0, width, height);
				commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				swapChain->PrepareBackBuffer(commandList);
				commandList->FlushBarriers();

				DX12DescriptorView rtvs[] =
				{
					swapChain->GetCurrentBackBufferView()
				};

				DX12Texture* dsTexture = rg.GetTexture(data.m_Depth);
				GGLAB_ASSERT_MSG(dsTexture != nullptr, "Resource must be Devirtualized.");
				auto dsvKey = DX12ViewCache::BuildKey<ViewType::DSV>(rg.GetResourceIndex(data.m_Depth), dsTexture);
				auto viewCache = rg.GetViewCache();
				auto& dsv = viewCache->GetOrCreate(dsvKey, dsTexture);

				commandList->SetRenderTargets(rtvs, &dsv);
				swapChain->ClearBackBuffer(commandList);
				commandList->ClearDepthStencil(dsv, 1.0f, 0);

				commandList->SetGraphicsConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::GlobalCB),
					renderer->GetFrameConstantBuffer()->GetBuffer()->Get()->GetGPUVirtualAddress());

				DrawModels(commandList);

				swapChain->FinishBackBuffer(commandList);
				commandList->FlushBarriers();

			});
	}


	void RenderPassForwardPBR::DrawModels(DX12CommandList* commandList) noexcept
	{
		auto& reg = Application::GetInstance()->GetEnttRegistry();
		auto* assetManager = Application::GetInstance()->GetAssetManager();

		auto view = reg.view<Model>();
		for (auto entity : view)
		{
			auto& model = view.get<Model>(entity);
			for (const auto& meshId : model.m_Meshes)
			{
				const auto* mesh = assetManager->GetMesh(meshId);
				if (!mesh || mesh->m_IndexCount == 0) { continue; }

				D3D12_VERTEX_BUFFER_VIEW vbs[] = { mesh->m_VertexBufferView };
				commandList->SetVertexBuffers(0, vbs);
				commandList->SetIndexBuffer(mesh->m_IndexBufferView);

				if (const auto* material = assetManager->GetMaterial(mesh->m_MaterialId);
					material && material->m_MaterialId.IsValid())
				{
					if (auto* texture = assetManager->GetTexture(material->m_BaseColorTex))
					{
						commandList->SetGraphicsDescriptor(
							static_cast<uint32_t>(CommonRSRootParamIndex::TextureDescriptorTable),
							texture->m_Descriptor);
					}
				}
				commandList->DrawIndexedInstanced(static_cast<uint32_t>(mesh->m_IndexCount));
			}
		}
	}

	// TODO: Add default base color descriptor when material or texture is missing.
	// TODO: Sort and batch draw calls by PSO, Material and Texture.
}