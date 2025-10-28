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
#include "AssetManager.h"
#include "Camera.h"
#include "RenderPassForwardPBR.h"
#include "MathUtils.h"

namespace gglab
{
	Renderer::Renderer() noexcept
	{
		m_Device = std::make_unique<DX12Device>();
		m_Device->Initialize();
	}

	void Renderer::Initialize() noexcept
	{
		m_GlobalCB = std::make_unique<DX12ConstantBuffer<FrameCBData>>(m_Device.get());
		m_RGGpuAllocator = std::make_unique<RGGpuResourceAllocator>(m_Device.get());

		DX12ViewCache::DescriptorsAllocatorArray descriptorAllocators =
		{
			m_Device->GetRtvDescriptorAllocator(),
			m_Device->GetDsvDescriptorAllocator(),
			m_Device->GetCbvSrvUavDescriptorAllocator()
		};
		m_ViewCache = std::make_unique<DX12ViewCache>(m_Device.get(), descriptorAllocators);
		m_PSOCache = std::make_unique<DX12PSOCache>(m_Device.get(), std::make_unique<StreamPSOCreator>());
		m_RootSignatureCache = std::make_unique<DX12RootSignatureCache>(m_Device.get());
		m_RenderPassRecipeRegistry = std::make_unique<RenderPassRecipeRegistry>(Application::GetInstance()->GetShaderManager());

		CreateCamera();
		CreateCommonRootSignature();

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
		RenderGraph::CreateInfo rgCreateInfo{};
		rgCreateInfo.m_GpuResourceAllocator = m_RGGpuAllocator.get();
		rgCreateInfo.m_ViewCache = m_ViewCache.get();
		RenderGraph rg(rgCreateInfo);

		RenderPassForwardPBR forwardPbrPass{};
		forwardPbrPass.AddPass(rg);

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

	DX12RootSignature* Renderer::GetCommonRootSignature() const noexcept
	{
		return m_RootSignatureCache->GetDX12RootSignature(m_CommonRootSignatureId);
	}

	void Renderer::CreateCommonRootSignature() noexcept
	{
		CD3DX12_DESCRIPTOR_RANGE1 range = {};
		range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 
			1, // numDescriptors
			0, // baseShaderRegister
			0, // registerSpace
			D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

		CD3DX12_ROOT_PARAMETER1 rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::RootParamCount)] = {};
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::GlobalCB)].
			InitAsConstantBufferView(0);
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::ObjectCB)].
			InitAsConstantBufferView(1);
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::MaterialCB)].
			InitAsConstantBufferView(2);
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::TextureDescriptorTable)].
			InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_PIXEL);

		constexpr uint32_t StaticSamplerCount = 1;
		CD3DX12_STATIC_SAMPLER_DESC staticSamplers[StaticSamplerCount] = {};
		staticSamplers[0].Init(0,
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.Init_1_1(
			static_cast<uint32_t>(CommonRSRootParamIndex::RootParamCount), rootParameters,
			StaticSamplerCount, staticSamplers,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

		auto [id, rootSig] = m_RootSignatureCache->GetOrCreate(rootSignatureDesc);
		m_CommonRootSignatureId = id;
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
		FrameCBData globalCB = {};

		Matrix viewMatrix = m_Camera->GetViewMatrix();
		Matrix projMatrix = m_Camera->GetProjMatrix();

		globalCB.ViewMat = DirectX::XMMatrixTranspose(viewMatrix);
		globalCB.ProjMat = DirectX::XMMatrixTranspose(projMatrix);
		globalCB.CameraPos = utils::ToVector4(m_Camera->GetPosition(), 1.0f);

		m_GlobalCB->Update(globalCB);
	}
}