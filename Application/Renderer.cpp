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
#include "TextureManager.h"
#include "Geometry.h"
#include "Mesh.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	Renderer::Renderer() noexcept
	{
		auto width = Application::Get()->GetWindowWidth();
		auto height = Application::Get()->GetWindowHeight();

		m_Device = std::make_unique<DX12Device>();
		m_Device->Initialize();
	}

	Renderer::~Renderer() noexcept
	{
	}

	void Renderer::Initialize() noexcept
	{
		// Init TextureManager
		m_TextureManager = std::make_unique<TextureManager>(m_Device.get());

		InitializeRootSignatures();
		InitializePipelineStates();
		InitializeRenderTargets();
		InitializeConstantBuffer();
		InitializeRenderObjects();

		m_IsInitialized = true;
	}

	void Renderer::Update() noexcept
	{
		UpdateGpuBuffers();
	}

	void Renderer::Render() noexcept
	{
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

		commandList->SetGraphicsRootSignature(*m_RootSignatures.at(static_cast<uint32_t>(RootSignatureIndex::CommonRootSignature)));
		commandList->SetPipelineState(*m_PipelineStates[static_cast<uint32_t>(PSOIndex::TexturedModelPSO)].get());
		commandList->SetDescriptorHeap(*cbvDescriptorHeap);

		commandList->SetViewport(0, 0, bufferWidth, bufferHeight);
		commandList->SetScissorRect(0, 0, bufferWidth, bufferHeight);

		commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		swapChain->PrepareBackBuffer(commandList);
		commandList->FlushBarriers();

		DX12Descriptor rtDescriptors[] = { swapChain->GetBackBufferDescriptor(swapChain->GetCurrentBackBufferIndex()) };
		auto& dsDescriptor = m_RenderTargetDescriptors[static_cast<uint32_t>(RenderTargetIndex::DS0)];
		commandList->SetRenderTargets(rtDescriptors, &dsDescriptor);

		swapChain->ClearBackBuffer(commandList);
		commandList->ClearDepthStencil(dsDescriptor, 1.0f);

		commandList->SetGraphicsConstantBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::ConstantBufferIndex), m_GlobalConstantBuffer->GetBuffer()->Get()->GetGPUVirtualAddress());

		// Render Object
		RenderObjects(commandList);

		swapChain->FinishBackBuffer(commandList);
		commandList->FlushBarriers();

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

	void Renderer::InitializeRootSignatures() noexcept
	{
		// Common RootSignature
		{
			CD3DX12_DESCRIPTOR_RANGE1 range = {};
			range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

			CD3DX12_ROOT_PARAMETER1 rootParameters[(uint32_t)CommonRSRootParamIndex::RootParamCount] = {};
			rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::ConstantBufferIndex)].InitAsConstantBufferView(0);
			rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::TextureDescriptorTable)].InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_PIXEL);

			CD3DX12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
			staticSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);


			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
			rootSignatureDesc.Init_1_1(
				static_cast<uint32_t>(CommonRSRootParamIndex::RootParamCount), rootParameters,
				1, staticSamplers,
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

			m_RootSignatures[static_cast<uint32_t>(RootSignatureIndex::CommonRootSignature)] = std::make_unique<DX12RootSignature>(m_Device.get(), rootSignatureDesc);
		}

	}

	void Renderer::InitializePipelineStates() noexcept
	{
		// Normal Texture Model PSO
		{
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
			rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
			//rasterizerDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;	// Wireframe mode for debugging
			rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

			D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.InputLayout = inputLayoutDesc;
			psoDesc.pRootSignature = m_RootSignatures[static_cast<uint32_t>(RootSignatureIndex::CommonRootSignature)]->Get();
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

			m_PipelineStates[static_cast<uint32_t>(PSOIndex::TexturedModelPSO)] = std::make_unique<DX12GraphicsPipelineState>(m_Device.get(), graphicsPSODesc);

		}
	}

	void Renderer::InitializeRenderTargets() noexcept
	{
		auto app = Application::Get();
		auto width = app->GetWindowWidth();
		auto height = app->GetWindowHeight();
		auto rtHeap = m_Device->GetRtvDescriptorHeap();

		// RenderTargets
		for (uint32_t i = 0; i < static_cast<uint32_t>(RenderTargetIndex::DS0); ++i)
		{
			auto rtFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
			auto rtResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(rtFormat, width, height);
			rtResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			const float rtClearColor[] = { 0.0f,0.4f,0.5f,1.0f };
			auto rtClearValue = CD3DX12_CLEAR_VALUE(rtFormat, rtClearColor);

			auto& rtTexture = m_RenderTargets[i];
			rtTexture = std::make_unique<DX12Texture>(m_Device.get(),
				D3D12_HEAP_TYPE_DEFAULT,
				rtResourceDesc,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				rtClearValue);

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
			dsTexture = std::make_unique<DX12Texture>(m_Device.get(),
				D3D12_HEAP_TYPE_DEFAULT,
				dsResourceDesc,
				D3D12_RESOURCE_STATE_DEPTH_WRITE,
				dsClearValue);

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
		// Load UVChecker Texture. TODO: Maybe load assets in Application? make a AssetDataBase?
		m_TextureManager->LoadTexture("Assets/textures/UVChecker1K.png");

		m_TestCube = std::make_unique<Cube>(m_Device.get());
		m_TestCube->Initialize();
	}

	void Renderer::UpdateGpuBuffers() noexcept
	{
		UpdateGlobalConstantBuffer();
	}

	void Renderer::UpdateGlobalConstantBuffer() noexcept
	{
		GlobalConstantBuffer buffer = {};

		static float angle = 0.0f;
		angle += 1.6f;
		const XMVECTOR rotationAxis = XMVectorSet(0.f, 1.f, 0.f, 0.f);
		XMMATRIX modelMatrix = XMMatrixRotationAxis(rotationAxis, XMConvertToRadians(angle));
		XMStoreFloat4x4(&buffer.m_ModelMatrix, modelMatrix);

		const XMVECTOR eyePosition = XMVectorSet(0.f, 10.f, -30.f, 0.f);
		const XMVECTOR focusPoint = XMVectorSet(0.f, 10.f, 0.f, 0.f);
		const XMVECTOR upDirection = XMVectorSet(0.f, 1.f, 0.f, 0.f);
		XMMATRIX viewMatrix = ::XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);
		auto swapChain = m_Device->GetSwapChain();

		float aspectRatio = static_cast<float>(swapChain->GetBufferWidth()) / swapChain->GetBufferHeight();
		XMMATRIX projectionMatrix = ::XMMatrixPerspectiveFovLH(XMConvertToRadians(45.0f), aspectRatio, 0.1f, 1000.f);

		XMStoreFloat4x4(&buffer.m_ViewMatrix, XMMatrixTranspose(viewMatrix));
		XMStoreFloat4x4(&buffer.m_ProjectionMatrix, XMMatrixTranspose(projectionMatrix));

		m_GlobalConstantBuffer->Update(buffer);
	}

	void Renderer::RenderObjects(DX12CommandList* commandList) noexcept
	{
		const auto& mesh = m_TestCube->GetMesh();

		D3D12_VERTEX_BUFFER_VIEW vertexBufferViews[] = { mesh->GetVertexBufferView() };
		commandList->SetVertexBuffers(0, vertexBufferViews);
		commandList->SetIndexBuffer(mesh->GetIndexBufferView());

		//commandList->DrawInstanced(mesh->GetVertexCount());
		commandList->DrawIndexedInstanced(mesh->GetIndexCount());

	}
}