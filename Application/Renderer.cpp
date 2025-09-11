#include "Precompiled.h"
#include "Renderer.h"
#include "Application.h"
#include "DX12Device.h"
#include "DX12RootSignature.h"
#include "DX12CommandList.h"
#include "DX12CommandQueue.h"
#include "DX12SwapChain.h"
#include "DX12CommandAllocator.h"
#include "DX12ConstantBuffer.h"
#include "RenderGraph.h"
#include "RGGpuResourceAllocator.h"
#include "Components.h"
#include "AssetManager.h"
#include "Camera.h"
#include "RenderPassTexColor.h"

namespace gglab
{
	Renderer::Renderer() noexcept
	{
		m_Device = std::make_unique<DX12Device>();
		m_Device->Initialize();
	}

	void Renderer::Initialize() noexcept
	{
		m_GlobalConstantBuffer = std::make_unique<DX12ConstantBuffer<GlobalConstantBuffer>>(m_Device.get());

		CreateCamera();
		CreateRenderObjects();
		CreateCommonRootSignature();
	
		m_RGGpuAllocator = std::make_unique<RGGpuResourceAllocator>(m_Device.get());
		m_TexColorPass = std::make_unique<RenderPassTexColor>(m_Device.get());

		ViewCache::DescriptorsAllocatorArray descriptorAllocators =
		{
			m_Device->GetRtvDescriptorAllocator(),
			m_Device->GetDsvDescriptorAllocator(),
			m_Device->GetCbvSrvUavDescriptorAllocator()
		};
		m_ViewCache = std::make_unique<ViewCache>(m_Device.get(), descriptorAllocators);
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
		auto backBufferIndex = swapChain->GetCurrentBackBufferIndex();
		auto commandAllocatorPool = m_Device->GetGraphicsCommandAllocatorPool();
		auto commandList = m_Device->GetGraphicsCommandList(backBufferIndex);
		auto commandQueue = m_Device->GetGraphicsCommandQueue();

		swapChain->WaitFrameCompletion();

		auto commandAllocator = commandAllocatorPool->RequestCommandAllocator();
		commandList->Begin(commandAllocator);

		// Render Graph
		RenderGraph rg(*m_RGGpuAllocator.get());

		m_TexColorPass->AddPass(rg);

		rg.Compile();

		RGExecuteContext executeContext = {};
		executeContext.m_GraphicsCommandList = commandList;
		rg.Execute(executeContext);

		commandList->End();

		const DX12CommandList* commandLists[] = { commandList };
		auto fencePoint = commandQueue->Execute(commandLists);

		rg.Retire(fencePoint);

		m_RGGpuAllocator->Tick();

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

		CD3DX12_ROOT_PARAMETER1 rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::RootParamCount)] = {};
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

	void Renderer::CreateRenderObjects() noexcept
	{
		auto& enttRegistry = Application::GetInstance()->GetEnttRegistry();
		auto* assetManager = Application::GetInstance()->GetAssetManager();

		// Test Sponza
		{
			auto model = assetManager->LoadModel("Assets/models/Sponza/Sponza.gltf");
			auto sponzaEntity = enttRegistry.create();
			enttRegistry.emplace<Transform>(sponzaEntity, Transform());
			enttRegistry.emplace<Model>(sponzaEntity, std::move(model));
		}
	}

	void Renderer::CreateCamera() noexcept
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
}