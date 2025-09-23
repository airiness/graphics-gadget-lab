#include "Precompiled.h"
#include "RenderPassTexColor.h"
#include "RenderGraph.h"
#include "DX12Device.h"
#include "DX12SwapChain.h"
#include "DX12CommandList.h"
#include "DX12ConstantBuffer.h"
#include "DX12RootSignature.h"
#include "DX12PSOCache.h"
#include "VertexData.h"
#include "Application.h"
#include "AssetManager.h"
#include "ShaderManager.h"
#include "Renderer.h"
#include "RenderPassRecipeRegistry.h"
#include "Components.h"
#include "Utility.h"

namespace gglab
{
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
			[this, &rg](DX12CommandList* commandList, TexColorData& data)
			{
				auto* renderer = Application::GetInstance()->GetRenderer();
				auto* shaderManager = Application::GetInstance()->GetShaderManager();
				auto* device = renderer->GetDevice();
				auto* passRegistry = renderer->GetRenderPassRecipeRegistry();
				auto* rootSignature = renderer->GetCommonRootSignature();
				auto* psoCache = renderer->GetPSOCache();
				auto* swapChain = device->GetSwapChain();

				const auto w = swapChain->GetBufferWidth();
				const auto h = swapChain->GetBufferHeight();

				// GraphicsKeyInputs build.
				auto vertexShaderId = shaderManager->LoadShader("Shaders/TexturedModelVS.dxil", ShaderStage::Vertex);
				auto pixelShaderId = shaderManager->LoadShader("Shaders/TexturedModelPS.dxil", ShaderStage::Pixel);

				GraphicsKeyInputs keyInputs = {};
				keyInputs.m_RootSignatureId = renderer->GetCommonRootSignatureId();
				keyInputs.m_VSId = vertexShaderId;
				keyInputs.m_PSId = pixelShaderId;
				keyInputs.m_VSGen = shaderManager->GetGeneration(vertexShaderId);
				keyInputs.m_PSGen = shaderManager->GetGeneration(pixelShaderId);
				keyInputs.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
				keyInputs.m_SampleMask = std::numeric_limits<uint32_t>::max();
				keyInputs.m_Formats.m_RtvCount = 1;
				keyInputs.m_Formats.m_Rtv[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
				keyInputs.m_Formats.m_Dsv = DXGI_FORMAT_D24_UNORM_S8_UINT;
				keyInputs.m_Formats.m_SampleCount = 1;
				keyInputs.m_Formats.m_SampleQuality = 0;

				// TODO: multi thread safety?
				const auto& cached = passRegistry->GetOrCreateGraphics(
					"RenderPassTexColor.main", keyInputs,
					[](GraphicsPipelineDesc& outDesc, const GraphicsKeyInputs& input, ShaderManager* sm)
					{
						auto* renderer = Application::GetInstance()->GetRenderer();
						outDesc.m_RootSignatureId = input.m_RootSignatureId;
						outDesc.m_RootSignature = renderer->GetRootSignatureCache()->GetDX12RootSignature(input.m_RootSignatureId)->Get();

						D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
							{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, m_Position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
							{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, m_Normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
							{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, m_TexCoord), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
						};
						outDesc.m_InputLayout.assign(std::begin(inputLayout), std::end(inputLayout));

						outDesc.m_VertexShader = sm->GetBytecode(input.m_VSId);
						outDesc.m_PixelShader = sm->GetBytecode(input.m_PSId);

						outDesc.m_Topology = input.m_Topology;
						outDesc.m_RasterizerDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
						outDesc.m_BlendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
						outDesc.m_DepthDesc = CD3DX12_DEPTH_STENCIL_DESC1(D3D12_DEFAULT);
						outDesc.m_SampleMask = input.m_SampleMask;

						outDesc.m_RtvCount = input.m_Formats.m_RtvCount;
						for (uint32_t i = 0; i < input.m_Formats.m_RtvCount; ++i)
						{
							outDesc.m_RtvFormats[i] = input.m_Formats.m_Rtv[i];
						}
						outDesc.m_DsvFormat = input.m_Formats.m_Dsv;
						outDesc.m_SampleCount = input.m_Formats.m_SampleCount;
						outDesc.m_SampleQuality = input.m_Formats.m_SampleQuality;
					});

				auto* pso = psoCache->GetOrCreate(cached.m_Key, cached.m_Desc);

				// TODO: last state cache to avoid redundant state setting.(root signature, pso)
				commandList->SetGraphicsRootSignature(*rootSignature);
				commandList->SetPipelineState(*pso);

				commandList->SetDescriptorHeap(*device->GetCbvSrvUavDescriptorAllocator()->GetHeap());

				commandList->SetViewport(0, 0, w, h);
				commandList->SetScissorRect(0, 0, w, h);
				commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				// back buffer on / clear
				swapChain->PrepareBackBuffer(commandList);
				commandList->FlushBarriers();

				DX12DescriptorView rtvs[] = {
					swapChain->GetBackBufferDescriptor(swapChain->GetCurrentBackBufferIndex())
				};

				DX12Texture* dsTexture = rg.GetTexture(data.m_Depth);
				GGLAB_ASSERT_MSG(dsTexture != nullptr, "Resource must be Devirtualized.");

				D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
				dsvDesc.Format = dsTexture->GetDesc().Format;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
				dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
				dsvDesc.Texture2D.MipSlice = 0;

				auto viewCache = rg.GetViewCache();
				auto& dsvDescriptor = viewCache->GetDSV(rg.GetResourceIndex(data.m_Depth), dsTexture, dsvDesc);

				commandList->SetRenderTargets(rtvs, &dsvDescriptor);
				swapChain->ClearBackBuffer(commandList);
				commandList->ClearDepthStencil(dsvDescriptor, 1.0f);

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