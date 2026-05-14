#include "Core/Precompiled.h"
#include "Graphics/Renderer.h"
#include "Graphics/DX12/DX12Device.h"
#include "Graphics/DX12/DX12RootSignature.h"
#include "Graphics/DX12/DX12CommandList.h"
#include "Graphics/DX12/DX12CommandQueue.h"
#include "Graphics/DX12/DX12SwapChain.h"
#include "Graphics/DX12/DX12CommandAllocator.h"
#include "Graphics/DX12/DX12ConstantBuffer.h"
#include "Graphics/DX12/DX12Fence.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGGpuResourceAllocator.h"
#include "Graphics/AssetManager.h"
#include "Graphics/TransferManager.h"
#include "Core/Components.h"
#include "Graphics/Camera.h"
#include "Graphics/RenderView.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderPass/RenderPassForwardPBR.h"
#include "Core/Utility/MathUtils.h"
#include "Core/Utility/TypeUtils.h"

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

		m_RGGpuResAllocator = std::make_unique<RGGpuResourceAllocator>(m_Device.get());

		DX12ViewCache::CreateInfo viewCacheCreateInfo{};
		viewCacheCreateInfo.m_DX12Device = m_Device.get();
		viewCacheCreateInfo.m_DescriptorManager = m_DescriptorManager.get();
		m_ViewCache = std::make_unique<DX12ViewCache>(viewCacheCreateInfo);

		m_PSOCache = std::make_unique<DX12PSOCache>(m_Device.get(), std::make_unique<StreamPSOCreator>());

		m_RootSignatureCache = std::make_unique<DX12RootSignatureCache>(m_Device.get());

		m_RenderPassRecipeRegistry = std::make_unique<RenderPassRecipeRegistry>(createInfo.m_ShaderManager);

		m_ExternalResRegistry = std::make_unique<RGExternalResourceRegistry>(m_ViewCache.get());

		SamplerRegistry::CreateInfo samplerRegistryCreateInfo{};
		samplerRegistryCreateInfo.m_DescriptorManager = m_DescriptorManager.get();
		m_SamplerRegistry = std::make_unique<SamplerRegistry>(samplerRegistryCreateInfo);
		m_SamplerRegistry->InitializePresetSamplers();

		RenderResourceRegistry::CreateInfo renderResRegistryCreateInfo{};
		renderResRegistryCreateInfo.m_DescriptorManager = m_DescriptorManager.get();
		renderResRegistryCreateInfo.m_ExternalResourceRegistry = m_ExternalResRegistry.get();
		renderResRegistryCreateInfo.m_RGGpuResAllocator = m_RGGpuResAllocator.get();
		renderResRegistryCreateInfo.m_SamplerRegistry = m_SamplerRegistry.get();
		m_RenderResRegistry = std::make_unique<RenderResourceRegistry>(renderResRegistryCreateInfo);

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
		
		m_RenderResRegistry.reset();
		m_ExternalResRegistry.reset();
		m_RenderPassRecipeRegistry.reset();
		m_RootSignatureCache.reset();
		m_PSOCache.reset();
		m_ViewCache.reset();
		m_RGGpuResAllocator.reset();

		m_SceneCB.reset();
		m_ObjectSB.reset();
		m_MaterialSB.reset();
		m_ViewSB.reset();

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

		m_RGGpuResAllocator->Tick();

		commandAllocatorPool->RecycleCommandAllocator(commandAllocator, m_LastSubmittedFencePoint);

		m_DescriptorManager->EndFrame(m_LastSubmittedFencePoint);

		swapChain->UpdateFrameSyncObject(m_LastSubmittedFencePoint);
		swapChain->Present();

	}

	DX12RootSignature* Renderer::GetCommonRootSignature() const noexcept
	{
		return m_RootSignatureCache->GetDX12RootSignature(m_CommonRootSignatureId);
	}

	void Renderer::UpdateFrameConstants(const RenderScene& scene) noexcept
	{
		// Update SceneCB
		SceneGPU sceneCB{};
		sceneCB.ObjectBaseIndex = scene.m_ObjectBaseIndex;
		sceneCB.ObjectCount = scene.m_ObjectCount;
		sceneCB.MaterialBaseIndex = scene.m_MaterialBaseIndex;
		sceneCB.MaterialCount = scene.m_MaterialCount;
		sceneCB.ViewBaseIndex = scene.m_ViewBaseIndex;
		sceneCB.ViewCount = scene.m_ViewCount;

		m_RenderResRegistry->EnsureIblResources();
		m_RenderResRegistry->FillIBLBindlessGPU(sceneCB.IBLResource);

		// Main Light
		sceneCB.MainLight = scene.m_MainLight;

		const auto currentIndex = m_SwapChain->GetCurrentBackBufferIndex();
		m_SceneCB->Update(sceneCB, currentIndex);
	}

	RenderGraph::CreateInfo Renderer::CreateRenderGraphCreateInfo() const noexcept
	{
		RenderGraph::CreateInfo rgCreateInfo{};
		rgCreateInfo.m_GpuResourceAllocator = m_RGGpuResAllocator.get();
		rgCreateInfo.m_ViewCache = m_ViewCache.get();
		rgCreateInfo.m_ExternalResourceRegistry = m_ExternalResRegistry.get();

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

		if (m_ExternalResRegistry)
		{
			const uint32_t bufferCount = m_SwapChain->GetBufferCount();
			for (uint32_t index = 0; index < bufferCount; ++index)
			{
				DX12Texture* backBuffer = m_SwapChain->GetBackBuffer(index);
				if (!backBuffer)
				{
					continue;
				}

				m_ExternalResRegistry->Forget(backBuffer, true);
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

		// b0: SceneCB
		rootParameters[utils::ToIndex(CommonRSRootParamIndex::SceneCB)].InitAsConstantBufferView(0);

		// b1: ObjectCB, num32BitValues: 2, shaderRegister: b1
		rootParameters[utils::ToIndex(CommonRSRootParamIndex::ObjectCB)].InitAsConstants(2, 1);

		// t1: ObjectSB
		rootParameters[utils::ToIndex(CommonRSRootParamIndex::ObjectSB)].InitAsShaderResourceView(1);

		// t2: materialSB
		rootParameters[utils::ToIndex(CommonRSRootParamIndex::MaterialSB)].InitAsShaderResourceView(2);

		// t3: ViewSB
		rootParameters[utils::ToIndex(CommonRSRootParamIndex::ViewSB)].InitAsShaderResourceView(3);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.Init_1_1(
			static_cast<UINT>(utils::EnumCount<CommonRSRootParamIndex>()),
			rootParameters,
			0,
			nullptr,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
			D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);

		auto [id, rootSig] = m_RootSignatureCache->GetOrCreate(rootSignatureDesc);
		m_CommonRootSignatureId = id;
	}

	void Renderer::InitializeGpuBuffers() noexcept
	{
		// Initialize Scene Constant Buffer
		{
			const auto constantBufferCount = DX12Device::GetBufferCount();
			m_SceneCB = std::make_unique<DX12ConstantBuffer<SceneGPU>>(m_Device.get(), constantBufferCount);
		}

		// Initialize Structured Buffers
		{
			DX12RingStructuredBuffer<ObjectGPU>::CreateInfo objectSBCreateInfo{};
			objectSBCreateInfo.m_DX12Device = m_Device.get();
			objectSBCreateInfo.m_ElementsCapacity = MaxObjectCapacity;
			m_ObjectSB = std::make_unique<DX12RingStructuredBuffer<ObjectGPU>>(objectSBCreateInfo);

			DX12RingStructuredBuffer<MaterialGPU>::CreateInfo materialSBCreateInfo{};
			materialSBCreateInfo.m_DX12Device = m_Device.get();
			materialSBCreateInfo.m_ElementsCapacity = MaxMaterialCapacity;
			m_MaterialSB = std::make_unique<DX12RingStructuredBuffer<MaterialGPU>>(materialSBCreateInfo);

			DX12RingStructuredBuffer<ViewGPU>::CreateInfo viewSBCreateInfo{};
			viewSBCreateInfo.m_DX12Device = m_Device.get();
			viewSBCreateInfo.m_ElementsCapacity = MaxViewCapacity;
			m_ViewSB = std::make_unique<DX12RingStructuredBuffer<ViewGPU>>(viewSBCreateInfo);
		}
	}
}