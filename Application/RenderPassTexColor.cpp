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
#include "InputLayoutLibrary.h"
#include "Components.h"
#include "PipelinePresets.h"

namespace gglab
{
	RenderPassTexColor::RenderPassTexColor() noexcept
	{
		// GraphicsKeyInputs build.
		auto* renderer = Application::GetInstance()->GetRenderer();
		auto* shaderManager = Application::GetInstance()->GetShaderManager();

		ShaderDesc shaderDesc{};
		shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassTexturedModel.hlsl";
		shaderDesc.m_Stage = ShaderStage::Vertex;
		auto vertexShaderId = shaderManager->LoadShader(shaderDesc);
		shaderDesc.m_Stage = ShaderStage::Pixel;
		auto pixelShaderId = shaderManager->LoadShader(shaderDesc);

		m_KeyInputs.m_RootSignatureId = renderer->GetCommonRootSignatureId();
		m_KeyInputs.m_InputLayoutId = InputLayoutId::P3N3T2;
		m_KeyInputs.m_VSId = vertexShaderId;
		m_KeyInputs.m_PSId = pixelShaderId;
		m_KeyInputs.m_VSGen = shaderManager->GetGeneration(vertexShaderId);
		m_KeyInputs.m_PSGen = shaderManager->GetGeneration(pixelShaderId);
		m_KeyInputs.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		m_KeyInputs.m_SampleMask = std::numeric_limits<uint32_t>::max();
		m_KeyInputs.m_Formats.m_RtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		m_KeyInputs.m_Formats.m_RtvFormats.NumRenderTargets = 1;
		m_KeyInputs.m_Formats.m_DsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		m_KeyInputs.m_Formats.m_SampleCount = 1;
		m_KeyInputs.m_Formats.m_SampleQuality = 0;
	}

	void RenderPassTexColor::AddPass(RenderGraph& rg) noexcept
	{
		struct TexColorData
		{
			RGTextureId m_Depth;
		};

		rg.AddPass<TexColorData>("RenderPassTexColor",
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
				auto* device = renderer->GetDevice();
				auto* passRegistry = renderer->GetRenderPassRecipeRegistry();
				auto* rootSignature = renderer->GetCommonRootSignature();
				auto* psoCache = renderer->GetPSOCache();
				auto* swapChain = device->GetSwapChain();

				const auto width = swapChain->GetBufferWidth();
				const auto height = swapChain->GetBufferHeight();

				// TODO: multi thread safety?
				const auto& cached = passRegistry->GetOrCreateGraphics(
					"RenderPassTexColor.main", m_KeyInputs,
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
				// TODO: last state cache to avoid redundant state setting.(root signature, pso)
				commandList->SetGraphicsRootSignature(*rootSignature);
				commandList->SetPipelineState(*pso);

				commandList->SetViewport(0, 0, width, height);
				commandList->SetScissorRect(0, 0, width, height);
				commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				// back buffer on / clear
				swapChain->PrepareBackBuffer(commandList);
				commandList->FlushBarriers();

				DX12DescriptorView rtvs[] = {
					swapChain->GetBackBufferDescriptor(swapChain->GetCurrentBackBufferIndex())
				};

				DX12Texture* dsTexture = rg.GetTexture(data.m_Depth);
				GGLAB_ASSERT_MSG(dsTexture != nullptr, "Resource must be Devirtualized.");

				auto dsvKey = DX12ViewCache::BuildKey<ViewType::DSV>(rg.GetResourceIndex(data.m_Depth), dsTexture);
				auto viewCache = rg.GetViewCache();
				auto& dsvDescriptor = viewCache->GetOrCreate(dsvKey, dsTexture);

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

					if (auto* material = assetManager->GetMaterial(mesh->m_MaterialId))
					{
						if (material->m_MaterialId.IsValid())
						{
							auto* texture = assetManager->GetTexture(material->m_BaseColorTex);
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