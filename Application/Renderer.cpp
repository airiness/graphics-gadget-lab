#include "Precompiled.h"
#include "Renderer.h"
#include "Application.h"
#include "DX12Device.h"
#include "DX12RootSignature.h"
#include "DX12PipelineState.h"
#include "DX12CommandList.h"
#include "DX12CommandQueue.h"
#include "DX12SwapChain.h"
#include "DX12Buffer.h"
#include "DX12Texture.h"
#include "DX12Fence.h"
#include "DX12Descriptor.h"
#include "DX12CommandAllocator.h"
#include "DX12ConstantBuffer.h"
#include "RenderGraph.h"
#include "Components.h"
#include "AssetManager.h"
#include "Camera.h"
#include "Geometry.h"
#include "RenderPassTexColor.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	Renderer::Renderer() noexcept
	{
		auto app = Application::GetInstance();
		auto width = app->GetWindowWidth();
		auto height = app->GetWindowHeight();

		m_Device = std::make_unique<DX12Device>();
		m_Device->Initialize();
	}

	Renderer::~Renderer() noexcept
	{
	}

	void Renderer::Initialize() noexcept
	{
		InitializeRenderTargets();
		InitializeConstantBuffer();
		InitializeCamera();
		InitializeRenderObjects();

		CreateCommonRootSignature();

		m_TexColorPass = std::make_unique<RenderPassTexColor>(m_Device.get());

		m_IsInitialized = true;
	}

	void Renderer::Update() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		m_Camera->Update();

		UpdateGpuBuffers();
	}

	void Renderer::Render() noexcept
	{
		if (m_IsInitialized == false)
		{
			return;
		}

		auto swapChain = m_Device->GetSwapChain();
		auto cbvDescriptorHeap = m_Device->GetCbvSrvUavDescriptorHeap();
		auto backBufferIndex = swapChain->GetCurrentBackBufferIndex();
		auto bufferWidth = swapChain->GetBufferWidth();
		auto bufferHeight = swapChain->GetBufferHeight();
		auto commandAllocatorPool = m_Device->GetGraphicsCommandAllocatorPool();
		auto commandList = m_Device->GetGraphicsCommandList(backBufferIndex);
		auto commandQueue = m_Device->GetGraphicsCommandQueue();

		swapChain->WaitFrameCompletion();

		auto commandAllocator = commandAllocatorPool->RequestCommandAllocator();
		commandList->Begin(commandAllocator);

		// Render Graph
		{
			RenderGraph rg(m_Device.get());

			m_TexColorPass->AddPass(rg);

			rg.Compile();

			RGExecuteContext executeContext = {};
			executeContext.m_GraphicsCommandList = commandList;
			rg.Execute(executeContext);

		}

		commandList->End();

		const DX12CommandList* commandLists[] = { commandList };
		auto fencePoint = commandQueue->Execute(commandLists);

		commandAllocatorPool->RecycleCommandAllocator(commandAllocator, fencePoint);

		swapChain->UpdateFrameSyncObject(std::move(fencePoint));
		swapChain->Present();

	}

	void Renderer::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		m_IsInitialized = false;

		m_Device->FlushGPU();
	}

	void Renderer::CreateCommonRootSignature() noexcept
	{
		CD3DX12_DESCRIPTOR_RANGE1 range = {};
		range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

		CD3DX12_ROOT_PARAMETER1 rootParameters[(uint32_t)CommonRSRootParamIndex::RootParamCount] = {};
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::ConstantBufferIndex)].InitAsConstantBufferView(0);
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::TextureDescriptorTable)].InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_PIXEL);

		CD3DX12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
		staticSamplers[0].Init(0,
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.Init_1_1(
			static_cast<uint32_t>(CommonRSRootParamIndex::RootParamCount), rootParameters,
			1, staticSamplers,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

		m_CommonRootSignature = std::make_unique<DX12RootSignature>(m_Device.get(), rootSignatureDesc);
	}

	const DX12Descriptor& Renderer::GetRenderTarget(RenderTargetIndex rtIndex) const noexcept
	{
		return m_RenderTargetDescriptors[static_cast<uint32_t>(rtIndex)];
	}

	void Renderer::InitializeRenderTargets() noexcept
	{
		auto app = Application::GetInstance();
		auto width = app->GetWindowWidth();
		auto height = app->GetWindowHeight();
		auto rtHeap = m_Device->GetRtvDescriptorHeap();

		// RenderTargets
		for (uint32_t i = 0; i < static_cast<uint32_t>(RenderTargetIndex::DS0); ++i)
		{
			auto rtFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
			auto rtResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(rtFormat, width, height);
			rtResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			const float rtClearColor[] = { 0.0f, 0.4f, 0.5f, 1.0f };
			auto rtClearValue = CD3DX12_CLEAR_VALUE(rtFormat, rtClearColor);

			auto& rtTexture = m_RenderTargets[i];
			rtTexture = std::make_unique<DX12Texture>();
			DX12Resource::CreateInfo rtCreateInfo = {};
			rtCreateInfo.m_Allocator = m_Device->GetMemAllocator();
			rtCreateInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
			rtCreateInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;
			rtCreateInfo.m_InitStates = D3D12_RESOURCE_STATE_RENDER_TARGET;
			rtCreateInfo.m_ResourceDesc = rtResourceDesc;
			rtCreateInfo.m_ClearValue = rtClearValue;
			rtTexture->Create(rtCreateInfo);
			
			// Create Render target view
			auto descriptor = rtHeap->CreateDescriptor();
			D3D12_RENDER_TARGET_VIEW_DESC rtDesc = {};
			rtDesc.Format = rtFormat;
			rtDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			rtDesc.Texture2D.MipSlice = 0;
			rtDesc.Texture2D.PlaneSlice = 0;
			m_Device->Get()->CreateRenderTargetView(rtTexture->Get(), &rtDesc, descriptor.m_CpuHandle);
			m_RenderTargetDescriptors[i] = descriptor;
		}

		auto dsHeap = m_Device->GetDsvDescriptorHeap();
		// DepthStencil
		{
			auto dsFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			auto dsResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(dsFormat, width, height);
			dsResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			auto dsClearValue = CD3DX12_CLEAR_VALUE(dsFormat, 1.0f, 0);

			auto& dsTexture = m_RenderTargets[static_cast<uint32_t>(RenderTargetIndex::DS0)];
			dsTexture = std::make_unique<DX12Texture>();
			DX12Resource::CreateInfo dsCreateInfo = {};
			dsCreateInfo.m_Allocator = m_Device->GetMemAllocator();
			dsCreateInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
			dsCreateInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;
			dsCreateInfo.m_InitStates = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			dsCreateInfo.m_ResourceDesc = dsResourceDesc;
			dsCreateInfo.m_ClearValue = dsClearValue;
			dsTexture->Create(dsCreateInfo);

			// Create depth stencil view
			auto descriptor = dsHeap->CreateDescriptor();
			D3D12_DEPTH_STENCIL_VIEW_DESC dsDesc = {};
			dsDesc.Format = dsFormat;
			dsDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			dsDesc.Flags = D3D12_DSV_FLAG_NONE;
			dsDesc.Texture2D.MipSlice = 0;
			m_Device->Get()->CreateDepthStencilView(dsTexture->Get(), &dsDesc, descriptor.m_CpuHandle);
			m_RenderTargetDescriptors[static_cast<uint32_t>(RenderTargetIndex::DS0)] = descriptor;
		}
	}

	void Renderer::InitializeConstantBuffer() noexcept
	{
		m_GlobalConstantBuffer = std::make_unique<DX12ConstantBuffer<GlobalConstantBuffer>>(m_Device.get());

		// Constant Buffer View
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbDesc = {};
		cbDesc.SizeInBytes = static_cast<UINT>(m_GlobalConstantBuffer->GetBufferSize());
		cbDesc.BufferLocation = m_GlobalConstantBuffer->GetBuffer()->Get()->GetGPUVirtualAddress();

		auto cbHeap = m_Device->GetCbvSrvUavDescriptorHeap();
		m_ConstantBufferDescriptor = cbHeap->CreateDescriptor();

		m_Device->Get()->CreateConstantBufferView(&cbDesc, m_ConstantBufferDescriptor.m_CpuHandle);
	}

	void Renderer::InitializeRenderObjects() noexcept
	{
		auto& enttRegistry = Application::GetInstance()->GetEnttRegistry();
		auto* assetManager = Application::GetInstance()->GetAssetManager();

		// Make a test cube 
		{
			//m_TestCube = primitiveGeometry::Cube::Create();
		}

		// Make a test module
		{
			//auto model = assetManager->LoadModel("Assets/models/FlightHelmet/FlightHelmet.gltf");
			//m_TestModel = enttRegistry.create();
			//enttRegistry.emplace<Transform>(m_TestModel, Transform());
			//enttRegistry.emplace<Model>(m_TestModel, std::move(model));
		}

		// Test Sponza
		{
			auto model = assetManager->LoadModel("Assets/models/Sponza/Sponza.gltf");
			auto sponzaEntity = enttRegistry.create();
			enttRegistry.emplace<Transform>(sponzaEntity, Transform());
			enttRegistry.emplace<Model>(sponzaEntity, std::move(model));
		}
	}

	void Renderer::InitializeCamera() noexcept
	{
		auto app = Application::GetInstance();

		Camera::Info info = {};
		info.m_Position = Vector3(-100.0f, 128.0f, 30.0f);
		info.m_Width = app->GetWindowWidth();
		info.m_Height = app->GetWindowHeight();
		info.m_Near = 0.1f;
		info.m_Far = 10000.0f;
		info.m_Fov = 60.0f;

		m_Camera = std::make_unique<Camera>(info);
	}

	void Renderer::UpdateGpuBuffers() noexcept
	{
		UpdateGlobalConstantBuffer();
	}

	void Renderer::UpdateGlobalConstantBuffer() noexcept
	{
		GlobalConstantBuffer cbBuffer = {};

		Matrix viewMatrix = m_Camera->GetViewMatrix();
		Matrix projMatrix = m_Camera->GetProjMatrix();

		cbBuffer.m_ViewMatrix = DirectX::XMMatrixTranspose(viewMatrix);
		cbBuffer.m_ProjectionMatrix = DirectX::XMMatrixTranspose(projMatrix);

		m_GlobalConstantBuffer->Update(cbBuffer);
	}

	void Renderer::RenderObjects(DX12CommandList* commandList) noexcept
	{
		auto& enttRegistry = Application::GetInstance()->GetEnttRegistry();

		const auto DrawModel = [](DX12CommandList* commandList, const Model& model)
			{
				auto* assetManager = Application::GetInstance()->GetAssetManager();
				for (const auto& mesh : model.m_Meshes)
				{
					auto* meshPtr = assetManager->GetMesh(mesh);
					if (meshPtr != nullptr)
					{
						D3D12_VERTEX_BUFFER_VIEW vertexBufferViews[] = { meshPtr->m_VertexBufferView };
						commandList->SetVertexBuffers(0, vertexBufferViews);
						commandList->SetIndexBuffer(meshPtr->m_IndexBufferView);
						auto* material = assetManager->GetMaterial(meshPtr->m_Material);
						if (material != nullptr && material->m_MaterialID != InvalidTextureID)
						{
							auto* texture = assetManager->GetTexture(material->m_TexBaseColor);
							commandList->SetGraphicsDescriptor(
								static_cast<uint32_t>(CommonRSRootParamIndex::TextureDescriptorTable),
								texture->m_Descriptor);
						}
						commandList->DrawIndexedInstanced(static_cast<uint32_t>(meshPtr->m_IndexCount));
					}
				}
			};

		// Get all models in the registry
		auto view = enttRegistry.view<Model>();
		for (auto entity : view)
		{
			auto& model = view.get<Model>(entity);
			DrawModel(commandList, model);
		}
	}
}