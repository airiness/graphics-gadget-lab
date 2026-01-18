#include "Precompiled.h"
#include "Renderer.h"
#include "DX12Device.h"
#include "DX12RootSignature.h"
#include "DX12CommandList.h"
#include "DX12CommandQueue.h"
#include "DX12SwapChain.h"
#include "DX12CommandAllocator.h"
#include "DX12ConstantBuffer.h"
#include "DX12Fence.h"
#include "RenderGraph.h"
#include "RGGpuResourceAllocator.h"
#include "AssetManager.h"
#include "TransferManager.h"
#include "Components.h"
#include "Camera.h"
#include "RenderView.h"
#include "RenderScene.h"
#include "RenderPassForwardPBR.h"
#include "MathUtils.h"
#include "TypeUtils.h"

namespace gglab
{
	void Renderer::Initialize(const CreateInfo& createInfo) noexcept
	{
		m_Device = std::make_unique<DX12Device>();
		m_Device->Initialize();

		m_SwapChain = std::make_unique<DX12SwapChain>();
		DX12SwapChain::CreateInfo swapChainCreateInfo{};
		swapChainCreateInfo.m_DX12Device = m_Device.get();
		swapChainCreateInfo.m_PresentQueue = m_Device->GetCommandQueue(CommandQueueType::Graphics);
		swapChainCreateInfo.m_Hwnd = createInfo.m_Hwnd;
		swapChainCreateInfo.m_Width = createInfo.m_Width;
		swapChainCreateInfo.m_Height = createInfo.m_Height;
		swapChainCreateInfo.m_BufferCount = DX12Device::GetBufferCount();
		swapChainCreateInfo.m_AllowTearing = m_Device->SupportTearing();

		m_SwapChain->Initialize(swapChainCreateInfo);

		DX12DescriptorManager::CreateInfo descriptorManagerCreateInfo{};
		descriptorManagerCreateInfo.m_DX12Device = m_Device.get();
		descriptorManagerCreateInfo.m_CbvSrvUavCount = 65536;
		descriptorManagerCreateInfo.m_RtvCount = 4096;
		descriptorManagerCreateInfo.m_DsvCount = 1024;
		descriptorManagerCreateInfo.m_SamplerCount = 2048;
		m_DescriptorManager = std::make_unique<DX12DescriptorManager>(descriptorManagerCreateInfo);

		m_TransferManager = std::make_unique<TransferManager>(m_Device.get(), createInfo.m_TransferManagerBufferSize);

		m_RGGpuAllocator = std::make_unique<RGGpuResourceAllocator>(m_Device.get());

		DX12ViewCache::CreateInfo viewCacheCreateInfo{};
		viewCacheCreateInfo.m_DX12Device = m_Device.get();
		viewCacheCreateInfo.m_DescriptorManager = m_DescriptorManager.get();
		m_ViewCache = std::make_unique<DX12ViewCache>(viewCacheCreateInfo);

		m_PSOCache = std::make_unique<DX12PSOCache>(m_Device.get(), std::make_unique<StreamPSOCreator>());

		m_RootSignatureCache = std::make_unique<DX12RootSignatureCache>(m_Device.get());

		m_RenderPassRecipeRegistry = std::make_unique<RenderPassRecipeRegistry>(createInfo.m_ShaderManager);

		m_ExternalResourceRegistry = std::make_unique<ExternalResourceRegistry>(m_ViewCache.get());

		m_DevelopGui = std::make_unique<DevelopGui>();
		DevelopGui::CreateInfo developGuiCreateInfo{};
		developGuiCreateInfo.m_Hwnd = createInfo.m_Hwnd;
		developGuiCreateInfo.m_DX12Device = m_Device.get();
		developGuiCreateInfo.m_SwapChain = m_SwapChain.get();
		developGuiCreateInfo.m_DescriptorManager = m_DescriptorManager.get();
		m_DevelopGui->Initialize(developGuiCreateInfo);

		CreateCommonRootSignature();
		InitializeGpuBuffers();

		m_IsInitialized = true;
	}

	void Renderer::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		m_IsSuspended.store(true, std::memory_order_relaxed);

		if (m_SwapChain)
		{
			m_SwapChain->Finalize();
			m_SwapChain.reset();
		}

		m_DevelopGui->Finalize();
		m_ExternalResourceRegistry.reset();
		m_RenderPassRecipeRegistry.reset();
		m_RootSignatureCache.reset();
		m_PSOCache.reset();
		m_ViewCache.reset();
		m_RGGpuAllocator.reset();

		m_FrameCB.reset();
		m_ObjectSB.reset();
		m_MaterialSB.reset();

		m_DescriptorManager->Tick();
		m_DescriptorManager.reset();
		m_TransferManager.reset();

		if (m_Device)
		{
			m_Device->Finalize();
			m_Device.reset();
		}

		m_IsInitialized = false;
	}

	void Renderer::Render(RenderGraph& rg, const RenderFrameContext& renderContext) noexcept
	{
		if (m_IsInitialized == false)
		{
			return;
		}

		// Window suspended do nothing
		if (m_IsSuspended.load(std::memory_order_relaxed))
		{
			return;
		}

		if (!m_SwapChain || !m_SwapChain->IsValid())
		{
			return;
		}

		auto swapChain = m_SwapChain.get();
		GGLAB_ASSERT(renderContext.m_BackBufferIndex == swapChain->GetCurrentBackBufferIndex());

		auto backBufferIndex = swapChain->GetCurrentBackBufferIndex();
		auto commandAllocatorPool = m_Device->GetCommandAllocatorPool(CommandQueueType::Graphics);
		auto commandList = m_Device->GetGraphicsCommandList(backBufferIndex);
		auto commandQueue = m_Device->GetCommandQueue(CommandQueueType::Graphics);

		// Wait Structured Buffer upload
		if (renderContext.m_UploadFencePoint.IsValid())
		{
			commandQueue->Wait(renderContext.m_UploadFencePoint);
		}

		swapChain->WaitFrameCompletion();

		auto commandAllocator = commandAllocatorPool->RequestCommandAllocator();
		commandList->Begin(commandAllocator);

		RGExecuteContext executeContext = {};
		executeContext.m_GraphicsCommandList = commandList;
		rg.Execute(executeContext);

		commandList->End();

		const DX12CommandList* commandLists[] = { commandList };
		m_LastSubmittedFencePoint = commandQueue->Execute(commandLists);

		rg.Retire(m_LastSubmittedFencePoint);

		m_RGGpuAllocator->Tick();

		commandAllocatorPool->RecycleCommandAllocator(commandAllocator, m_LastSubmittedFencePoint);

		m_DescriptorManager->EndFrame(m_LastSubmittedFencePoint);

		swapChain->UpdateFrameSyncObject(m_LastSubmittedFencePoint);
		swapChain->Present();

	}

	DX12RootSignature* Renderer::GetCommonRootSignature() const noexcept
	{
		return m_RootSignatureCache->GetDX12RootSignature(m_CommonRootSignatureId);
	}

	void Renderer::UpdateFrameConstants(const RenderView& view, const RenderScene& scene) noexcept
	{
		// Update FrameCB
		FrameCBData frameCB{};
		frameCB.ViewMat = view.m_View;
		frameCB.ProjMat = view.m_Proj;
		frameCB.CameraPos = utils::ToVector4(view.m_CameraPosition, 1.0f);
		frameCB.Exposure = 1.0f;	// TODO: camera exposure

		frameCB.ObjectBaseIndex = scene.m_ObjectBaseIndex;
		frameCB.MaterialBaseIndex = scene.m_MaterialBaseIndex;

		frameCB.MainLight = scene.m_MainLight;

		const auto currentIndex = m_SwapChain->GetCurrentBackBufferIndex();
		m_FrameCB->Update(frameCB, currentIndex);
	}

	RenderGraph::CreateInfo Renderer::CreateRenderGraphCreateInfo() const noexcept
	{
		RenderGraph::CreateInfo rgCreateInfo{};
		rgCreateInfo.m_GpuResourceAllocator = m_RGGpuAllocator.get();
		rgCreateInfo.m_ViewCache = m_ViewCache.get();
		rgCreateInfo.m_ExternalResourceRegistry = m_ExternalResourceRegistry.get();

		return rgCreateInfo;
	}

	void Renderer::OnResize(uint32_t width, uint32_t height) noexcept
	{
		if (!m_IsInitialized)
		{
			GGLAB_LOG_GRAPHICS_WARN("Renderer::OnResize. Renderer is not initialized.");
			return;
		}

		if (m_IsSuspended.load(std::memory_order_relaxed))
		{
			GGLAB_LOG_GRAPHICS_WARN("Renderer::OnResize ignored because renderer is suspended.");
			return;
		}

		if (!m_SwapChain || !m_SwapChain->IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN("Renderer::OnResize. SwapChain is invalid.");
			return;
		}

		if (width == 0 || height == 0)
		{
			GGLAB_LOG_GRAPHICS_WARN("Renderer::OnResize. Invalid resize resolution.");
			return;
		}

		m_Device->FlushGPU();

		if (m_ExternalResourceRegistry)
		{
			const uint32_t bufferCount = m_SwapChain->GetBufferCount();
			for (uint32_t index = 0; index < bufferCount; ++index)
			{
				DX12Texture* backBuffer = m_SwapChain->GetBackBuffer(index);
				if (!backBuffer)
				{
					continue;
				}

				m_ExternalResourceRegistry->Forget(backBuffer, true);
			}
		}

		m_SwapChain->OnResize(width, height);
	}

	void Renderer::OnSuspend() noexcept
	{
		m_IsSuspended.store(true, std::memory_order_relaxed);
	}

	void Renderer::OnResume() noexcept
	{
		m_IsSuspended.store(false, std::memory_order_relaxed);
	}

	bool Renderer::IsSuspended() const noexcept
	{
		return m_IsSuspended.load(std::memory_order_relaxed);
	}

	void Renderer::CreateCommonRootSignature() noexcept
	{
		CD3DX12_DESCRIPTOR_RANGE1 range = {};
		range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			1, // numDescriptors: t0
			0, // baseShaderRegister
			0, // registerSpace
			D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
		// D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE

		CD3DX12_ROOT_PARAMETER1 rootParameters[utils::EnumCount<CommonRSRootParamIndex>()] = {};

		// b0: FrameCB
		rootParameters[utils::ToIndex(CommonRSRootParamIndex::FrameCB)].InitAsConstantBufferView(0);

		// b1: ObjectCB, num32BitValues: 1, shaderRegister: b1
		rootParameters[utils::ToIndex(CommonRSRootParamIndex::ObjectCB)].InitAsConstants(1, 1);

		// t1: ObjectSB
		rootParameters[utils::ToIndex(CommonRSRootParamIndex::ObjectSB)].InitAsShaderResourceView(1);

		// t2: materialSB
		rootParameters[utils::ToIndex(CommonRSRootParamIndex::MaterialSB)].InitAsShaderResourceView(2);

		constexpr uint32_t StaticSamplerCount = 1;
		CD3DX12_STATIC_SAMPLER_DESC staticSamplers[StaticSamplerCount] = {};
		staticSamplers[0].Init(
			0,	// register s0
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.Init_1_1(
			static_cast<UINT>(utils::EnumCount<CommonRSRootParamIndex>()),
			rootParameters,
			StaticSamplerCount,
			staticSamplers,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
			// D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED

		auto [id, rootSig] = m_RootSignatureCache->GetOrCreate(rootSignatureDesc);
		m_CommonRootSignatureId = id;
	}

	void Renderer::InitializeGpuBuffers() noexcept
	{
		// Initialize global constant buffer
		{
			const auto constantBufferFrames = DX12Device::GetBufferCount();
			m_FrameCB = std::make_unique<DX12ConstantBuffer<FrameCBData>>(m_Device.get(), constantBufferFrames);
		}

		// Initialize structured buffers objects and materials
		{
			DX12RingStructuredBuffer<ObjectGPU>::CreateInfo objectSBCreateInfo{};
			objectSBCreateInfo.m_DX12Device = m_Device.get();
			objectSBCreateInfo.m_ElementsCapacity = MaxObjectCapacity;
			m_ObjectSB = std::make_unique<DX12RingStructuredBuffer<ObjectGPU>>(objectSBCreateInfo);

			DX12RingStructuredBuffer<MaterialGPU>::CreateInfo materialSBCreateInfo{};
			materialSBCreateInfo.m_DX12Device = m_Device.get();
			materialSBCreateInfo.m_ElementsCapacity = MaxMaterialCapacity;
			m_MaterialSB = std::make_unique<DX12RingStructuredBuffer<MaterialGPU>>(materialSBCreateInfo);
		}
	}
}