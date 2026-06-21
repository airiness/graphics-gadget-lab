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
	Renderer::Frame::~Frame() noexcept
	{
		if (m_Renderer && m_State != State::Ended)
		{
			m_Renderer->AbortFrame(*this);
		}
	}

	Renderer::~Renderer()
	{
		GGLAB_ASSERT_MSG(!m_HasActiveFrame,
			"Renderer destroyed while a Renderer::Frame is still active.");
	}

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

		TextureRegistry::CreateInfo textureRegistryCreateInfo{};
		textureRegistryCreateInfo.m_DX12Device = m_Device.get();
		textureRegistryCreateInfo.m_TransferManager = m_TransferManager.get();
		textureRegistryCreateInfo.m_DescriptorManager = m_DescriptorManager.get();
		m_TextureRegistry = std::make_unique<TextureRegistry>(textureRegistryCreateInfo);
		m_TextureRegistry->InitializeReservedTextures();

		RenderResourceRegistry::CreateInfo renderResRegistryCreateInfo{};
		renderResRegistryCreateInfo.m_DescriptorManager = m_DescriptorManager.get();
		renderResRegistryCreateInfo.m_ExternalResourceRegistry = m_ExternalResRegistry.get();
		renderResRegistryCreateInfo.m_RGGpuResAllocator = m_RGGpuResAllocator.get();
		renderResRegistryCreateInfo.m_SamplerRegistry = m_SamplerRegistry.get();
		m_RenderResRegistry = std::make_unique<RenderResourceRegistry>(renderResRegistryCreateInfo);

		m_DevelopGuiBackend = std::make_unique<DevelopGuiBackend>();
		DevelopGuiBackend::CreateInfo developGuiBackendCreateInfo{};
		developGuiBackendCreateInfo.m_Hwnd = createInfo.m_Hwnd;
		developGuiBackendCreateInfo.m_DX12Device = m_Device.get();
		developGuiBackendCreateInfo.m_SwapChain = m_SwapChain.get();
		developGuiBackendCreateInfo.m_DescriptorManager = m_DescriptorManager.get();
		m_DevelopGuiBackend->Initialize(developGuiBackendCreateInfo);

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

		GGLAB_ASSERT_MSG(!m_HasActiveFrame,
			"Renderer::Finalize called while a Renderer::Frame is still active.");

		m_IsSuspended.store(true, std::memory_order_relaxed);

		if (m_SwapChain)
		{
			m_SwapChain->Finalize();
			m_SwapChain.reset();
		}

		m_DevelopGuiBackend->Finalize();
		m_DevelopGuiBackend.reset();

		m_RenderResRegistry.reset();
		m_TextureRegistry->Finalize(m_LastSubmittedFencePoint);
		m_TextureRegistry.reset();
		m_SamplerRegistry.reset();
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

	Renderer::Frame Renderer::BeginFrame(uint32_t backBufferIndex) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::BeginFrame called before initialization.");
		GGLAB_ASSERT_MSG(!m_HasActiveFrame,
			"Renderer::BeginFrame called without ending the previous frame.");
		GGLAB_ASSERT_NOT_NULL(m_SwapChain.get());
		GGLAB_ASSERT(m_SwapChain->GetCurrentBackBufferIndex() == backBufferIndex);

		m_SwapChain->WaitFrameCompletion();

		if (m_LastSubmittedFencePoint.IsValid())
		{
			const auto completedGraphicsFence =
				m_LastSubmittedFencePoint.GetFence()->GetCompletedValue();
			m_ObjectSB->ReclaimCompleted(completedGraphicsFence);
			m_MaterialSB->ReclaimCompleted(completedGraphicsFence);
			m_ViewSB->ReclaimCompleted(completedGraphicsFence);
		}

		m_RGGpuResAllocator->Tick();
		m_DescriptorManager->Tick();

		m_HasActiveFrame = true;
		return Frame(this, backBufferIndex);
	}

	void Renderer::Render(
		Frame& frame,
		RenderGraph& rg,
		const RenderFrameContext& renderContext) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::Render called before initialization.");
		GGLAB_ASSERT_MSG(frame.m_Renderer == this,
			"Renderer::Render received a frame created by another Renderer.");
		GGLAB_ASSERT_MSG(m_HasActiveFrame && frame.m_State == Frame::State::Begun,
			"Renderer::Render requires an active frame begun by Renderer::BeginFrame.");
		GGLAB_ASSERT(renderContext.m_BackBufferIndex == frame.m_BackBufferIndex);

		frame.m_RenderGraph = &rg;
		frame.m_UploadFencePoint = renderContext.m_UploadFencePoint;
		if (renderContext.m_SceneGpuAllocations)
		{
			frame.m_SceneGpuAllocations = *renderContext.m_SceneGpuAllocations;
			*renderContext.m_SceneGpuAllocations = {};
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

		auto backBufferIndex = frame.m_BackBufferIndex;
		auto commandAllocatorPool = m_Device->GetCommandAllocatorPool(CommandQueueType::Graphics);
		auto commandList = m_Device->GetGraphicsCommandList(backBufferIndex);
		auto commandQueue = m_Device->GetCommandQueue(CommandQueueType::Graphics);

		// Wait Structured Buffer upload
		if (renderContext.m_UploadFencePoint.IsValid())
		{
			commandQueue->Wait(renderContext.m_UploadFencePoint);
		}

		frame.m_CommandAllocator = commandAllocatorPool->RequestCommandAllocator();
		commandList->Begin(frame.m_CommandAllocator);

		RGExecuteContext executeContext = {};
		executeContext.m_GraphicsCommandList = commandList;
		rg.Execute(executeContext);

		commandList->End();
		frame.m_State = Frame::State::Recorded;
	}

	void Renderer::EndFrame(Frame& frame) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::EndFrame called before initialization.");
		GGLAB_ASSERT_MSG(frame.m_Renderer == this,
			"Renderer::EndFrame received a frame created by another Renderer.");
		GGLAB_ASSERT_MSG(m_HasActiveFrame && frame.m_State != Frame::State::Ended,
			"Renderer::EndFrame called without a matching Renderer::BeginFrame.");

		if (frame.m_State != Frame::State::Recorded)
		{
			AbortFrame(frame);
			return;
		}

		GGLAB_ASSERT_NOT_NULL(m_SwapChain.get());
		GGLAB_ASSERT_NOT_NULL(frame.m_CommandAllocator);
		GGLAB_ASSERT_NOT_NULL(frame.m_RenderGraph);

		auto* swapChain = m_SwapChain.get();
		auto* commandAllocatorPool =
			m_Device->GetCommandAllocatorPool(CommandQueueType::Graphics);
		auto* commandList = m_Device->GetGraphicsCommandList(frame.m_BackBufferIndex);
		auto* commandQueue = m_Device->GetCommandQueue(CommandQueueType::Graphics);

		const DX12CommandList* commandLists[] = { commandList };
		m_LastSubmittedFencePoint = commandQueue->Execute(commandLists);
		RetireSceneGpuAllocations(
			&frame.m_SceneGpuAllocations,
			m_LastSubmittedFencePoint);

		frame.m_RenderGraph->Retire(m_LastSubmittedFencePoint);

		commandAllocatorPool->RecycleCommandAllocator(
			frame.m_CommandAllocator,
			m_LastSubmittedFencePoint);

		m_DescriptorManager->EndFrame(m_LastSubmittedFencePoint);

		swapChain->UpdateFrameSyncObject(m_LastSubmittedFencePoint);
		swapChain->Present();
		EndFrameLifetime(frame);
	}

	void Renderer::AbortFrame(Frame& frame) noexcept
	{
		if (frame.m_State == Frame::State::Ended)
		{
			return;
		}

		GGLAB_ASSERT_MSG(frame.m_Renderer == this,
			"Renderer::AbortFrame received a frame created by another Renderer.");

		auto* graphicsQueue = m_Device ?
			m_Device->GetCommandQueue(CommandQueueType::Graphics) :
			nullptr;
		if (graphicsQueue && frame.m_UploadFencePoint.IsValid())
		{
			graphicsQueue->Wait(frame.m_UploadFencePoint);
		}

		if (graphicsQueue)
		{
			// No draw commands consume this frame's resources, but uploads may
			// still be in flight. Signal after the queue wait so every frame-owned
			// allocation can retire on the graphics timeline.
			m_LastSubmittedFencePoint = graphicsQueue->Signal();
			RetireSceneGpuAllocations(
				&frame.m_SceneGpuAllocations,
				m_LastSubmittedFencePoint);

			if (frame.m_RenderGraph)
			{
				frame.m_RenderGraph->Retire(m_LastSubmittedFencePoint);
			}

			if (frame.m_CommandAllocator)
			{
				auto* commandAllocatorPool =
					m_Device->GetCommandAllocatorPool(CommandQueueType::Graphics);
				commandAllocatorPool->RecycleCommandAllocator(
					frame.m_CommandAllocator,
					m_LastSubmittedFencePoint);
			}

			m_DescriptorManager->EndFrame(m_LastSubmittedFencePoint);
		}

		EndFrameLifetime(frame);
	}

	void Renderer::EndFrameLifetime(Frame& frame) noexcept
	{
		frame.m_State = Frame::State::Ended;
		frame.m_CommandAllocator = nullptr;
		frame.m_RenderGraph = nullptr;
		frame.m_UploadFencePoint = {};
		frame.m_SceneGpuAllocations = {};
		frame.m_Renderer = nullptr;
		m_HasActiveFrame = false;
	}

	void Renderer::RetireSceneGpuAllocations(
		RenderSceneGpuAllocations* allocations,
		const DX12FencePoint& fencePoint) noexcept
	{
		if (!allocations || allocations->IsEmpty())
		{
			return;
		}

		GGLAB_ASSERT_MSG(fencePoint.IsValid(),
			"Scene GPU allocations require a valid graphics fence point.");

		if (allocations->m_Objects.IsValid())
		{
			m_ObjectSB->Retire(allocations->m_Objects, fencePoint);
		}
		if (allocations->m_Materials.IsValid())
		{
			m_MaterialSB->Retire(allocations->m_Materials, fencePoint);
		}
		if (allocations->m_Views.IsValid())
		{
			m_ViewSB->Retire(allocations->m_Views, fencePoint);
		}
		*allocations = {};
	}

	DX12RootSignature* Renderer::GetCommonRootSignature() const noexcept
	{
		return m_RootSignatureCache->GetDX12RootSignature(m_CommonRootSignatureId);
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

		// b1: DrawConstants
		rootParameters[utils::ToIndex(CommonRSRootParamIndex::DrawConstants)].InitAsConstants(
			MaxDrawConstantDWORDs, 1);

		// b2: PassConstants
		rootParameters[utils::ToIndex(CommonRSRootParamIndex::PassConstants)].InitAsConstants(
			MaxPassConstantDWORDs, 2);

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
			objectSBCreateInfo.m_ElementsCapacity =
				MaxObjectCapacity * DX12Device::GetBufferCount();
			m_ObjectSB = std::make_unique<DX12RingStructuredBuffer<ObjectGPU>>(objectSBCreateInfo);

			DX12RingStructuredBuffer<MaterialGPU>::CreateInfo materialSBCreateInfo{};
			materialSBCreateInfo.m_DX12Device = m_Device.get();
			materialSBCreateInfo.m_ElementsCapacity =
				MaxMaterialCapacity * DX12Device::GetBufferCount();
			m_MaterialSB = std::make_unique<DX12RingStructuredBuffer<MaterialGPU>>(materialSBCreateInfo);

			DX12RingStructuredBuffer<ViewGPU>::CreateInfo viewSBCreateInfo{};
			viewSBCreateInfo.m_DX12Device = m_Device.get();
			viewSBCreateInfo.m_ElementsCapacity =
				MaxViewCapacity * DX12Device::GetBufferCount();
			m_ViewSB = std::make_unique<DX12RingStructuredBuffer<ViewGPU>>(viewSBCreateInfo);
		}
	}
}
