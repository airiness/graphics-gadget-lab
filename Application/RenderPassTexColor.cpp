#include "Precompiled.h"
#include "RenderPassTexColor.h"
#include "RenderGraph.h"
#include "DX12Device.h"
#include "DX12SwapChain.h"
#include "DX12CommandList.h"
#include "DX12ConstantBuffer.h"
#include "DX12RootSignature.h"
#include "DX12PipelineState.h"
#include "VertexData.h"
#include "Application.h"
#include "AssetManager.h"
#include "Renderer.h"
#include "Components.h"
#include "Utility.h"

namespace gglab
{
	RenderPassTexColor::RenderPassTexColor(DX12Device* dx12Device) noexcept :
		m_DX12Device(dx12Device)
	{
		InitializePSO();
	}

	void RenderPassTexColor::AddPass(RenderGraph& rg) noexcept
	{
		struct TexColorData 
		{
			RGTextureId m_Depth;
		};

		const auto& texColorPass = rg.AddPass<TexColorData>("RenderPassTexColor",
			[this](RenderGraph::RGBuilder& builder, TexColorData& data)
			{
				builder.SideEffect();

				auto* swapChain = m_DX12Device->GetSwapChain();

				RGTextureDesc dsDesc = {};
				dsDesc.m_Width = swapChain->GetBufferWidth();
				dsDesc.m_Height = swapChain->GetBufferHeight();
				dsDesc.m_Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
				dsDesc.m_Usage = RGTextureUsage::DepthStencil;

				data.m_Depth = builder.CreateTexture("Depth", dsDesc);

				builder.Write<RGTextureResource>(data.m_Depth, RGTextureUsage::DepthStencil);

			},
			[this, &rg](DX12CommandList* commandList, TexColorData& data)
			{
				auto* renderer = Application::GetInstance()->GetRenderer();
				auto* rootSignature = renderer->GetCommonRootSignature();

				auto* swapChain = m_DX12Device->GetSwapChain();

				const uint32_t w = swapChain->GetBufferWidth();
				const uint32_t h = swapChain->GetBufferHeight();

				commandList->SetGraphicsRootSignature(*rootSignature);
				commandList->SetPipelineState(*m_PSO);
				commandList->SetDescriptorHeap(*m_DX12Device->GetCbvSrvUavDescriptorHeap());

				commandList->SetViewport(0, 0, w, h);
				commandList->SetScissorRect(0, 0, w, h);
				commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				// backbuffer on / clear
				swapChain->PrepareBackBuffer(commandList);
				commandList->FlushBarriers();

				DX12Descriptor rtvs[] = {
					swapChain->GetBackBufferDescriptor(swapChain->GetCurrentBackBufferIndex())
				};

				DX12Texture* dsTexture = rg.GetTexture(data.m_Depth);
				GGLAB_ASSERT_MSG(dsTexture != nullptr, "Resource must be Devirtualized.");
				auto dsHeap = m_DX12Device->GetDsvDescriptorHeap(); //TODO: view cache?
				auto dsDescriptorHandle = dsHeap->CreateDescriptor();
				D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
				dsvDesc.Format = dsTexture->GetDesc().Format;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
				dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
				dsvDesc.Texture2D.MipSlice = 0;

				m_DX12Device->Get()->CreateDepthStencilView(dsTexture->Get(), &dsvDesc, dsDescriptorHandle.m_CpuHandle);
				commandList->SetRenderTargets(rtvs, &dsDescriptorHandle);

				swapChain->ClearBackBuffer(commandList);
				commandList->ClearDepthStencil(dsDescriptorHandle, 1.0f);

				// globals
				commandList->SetGraphicsConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ConstantBufferIndex),
					renderer->GetGlobalConstantBuffer()->GetBuffer()->Get()->GetGPUVirtualAddress());

				// draw scene
				DrawModels(commandList);

				// backbuffer off
				swapChain->FinishBackBuffer(commandList);
				commandList->FlushBarriers();
			});
	}

	void RenderPassTexColor::InitializePSO() noexcept
	{
		auto* renderer = Application::GetInstance()->GetRenderer();
		auto* commonRootSignature = renderer->GetCommonRootSignature();

		ComPtr<ID3DBlob> vertexShaderBlob;
		utility::ThrowIfFailed(D3DReadFileToBlob(L"TexturedModelVS.cso", &vertexShaderBlob));

		ComPtr<ID3DBlob> pixelShaderBlob;
		utility::ThrowIfFailed(D3DReadFileToBlob(L"TexturedModelPS.cso", &pixelShaderBlob));

		// Input Layout
		D3D12_INPUT_ELEMENT_DESC inputLayout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, m_Position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, m_Normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, m_TexCoord), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
		inputLayoutDesc.pInputElementDescs = inputLayout;
		inputLayoutDesc.NumElements = ARRAYSIZE(inputLayout);

		CD3DX12_RASTERIZER_DESC rasterizerDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
		//rasterizerDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;	// Wireframe mode for debugging
		rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = inputLayoutDesc;
		psoDesc.pRootSignature = commonRootSignature->Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
		psoDesc.RasterizerState = rasterizerDesc;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC2(D3D12_DEFAULT);
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;

		DX12GraphicsPipelineStateDesc graphicsPSODesc = {};
		graphicsPSODesc.m_Desc = psoDesc;

		m_PSO = std::make_unique<DX12GraphicsPipelineState>(m_DX12Device, graphicsPSODesc);
	}

	void RenderPassTexColor::DrawModels(DX12CommandList* commandList) noexcept
	{
		auto& reg = Application::GetInstance()->GetEnttRegistry();
		auto* assetManager = Application::GetInstance()->GetAssetManager();

		const auto drawModel = [&assetManager, &commandList](const Model& model)
			{
				for (const auto& meshId : model.m_Meshes)
				{
					auto* mesh = assetManager->GetMesh(meshId);
					if (!mesh)
					{
						continue;
					}

					D3D12_VERTEX_BUFFER_VIEW vbs[] = { mesh->m_VertexBufferView };
					commandList->SetVertexBuffers(0, vbs);
					commandList->SetIndexBuffer(mesh->m_IndexBufferView);

					if (auto* material = assetManager->GetMaterial(mesh->m_Material))
					{
						if (material->m_MaterialID != InvalidTextureID)
						{
							auto* texture = assetManager->GetTexture(material->m_TexBaseColor);
							commandList->SetGraphicsDescriptor(static_cast<uint32_t>(CommonRSRootParamIndex::TextureDescriptorTable), texture->m_Descriptor);
						}
					}

					commandList->DrawIndexedInstanced(static_cast<uint32_t>(mesh->m_IndexCount));
				}
			};

		auto view = reg.view<Model>();
		for (auto entity : view)
		{
			drawModel(view.get<Model>(entity));
		}
	}
}
