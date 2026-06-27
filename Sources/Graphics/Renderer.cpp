#include "Core/Precompiled.h"
#include "Graphics/Renderer.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12CommandContext.h"
#include "Graphics/RHI/DX12/DX12RootSignature.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12CommandAllocator.h"
#include "Graphics/RHI/DX12/DX12Fence.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGTransientResourcePool.h"
#include "Graphics/AssetManager.h"
#include "Graphics/TransferManager.h"
#include "Graphics/Camera.h"
#include "Graphics/RenderView.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderPass/RenderPassForwardPBR.h"
#include "Core/Utility/MathUtils.h"
#include "Core/Utility/TypeUtils.h"

namespace gglab
{
	namespace
	{
		void AddBindingSlot(
			RHIBindingLayoutDesc& desc,
			RHIBindingType type,
			RHIShaderStage visibility,
			uint32_t binding,
			uint32_t space,
			uint32_t count,
			uint32_t sizeInBytes,
			const char* debugName) noexcept
		{
			GGLAB_ASSERT(desc.m_SlotCount < RHIBindingLayoutDesc::MaxSlots);
			auto& slot = desc.m_Slots[desc.m_SlotCount++];
			slot.m_Type = type;
			slot.m_Visibility = visibility;
			slot.m_Binding = binding;
			slot.m_Space = space;
			slot.m_Count = count;
			slot.m_SizeInBytes = sizeInBytes;
			slot.m_DebugName = debugName;
		}
	}

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

		RHISwapChainDesc swapChainCreateInfo{};
		swapChainCreateInfo.m_WindowHandle = createInfo.m_Hwnd;
		swapChainCreateInfo.m_Width = createInfo.m_Width;
		swapChainCreateInfo.m_Height = createInfo.m_Height;
		m_SwapChain = m_Device->CreateSwapChain(swapChainCreateInfo);
		GGLAB_ASSERT_MSG(m_SwapChain && m_SwapChain->IsValid(),
			"Renderer failed to create an RHI swapchain.");

		DX12DescriptorManager::CreateInfo descriptorManagerCreateInfo{};
		descriptorManagerCreateInfo.m_DX12Device = m_Device.get();
		descriptorManagerCreateInfo.m_CbvSrvUavCount = 65536;
		descriptorManagerCreateInfo.m_RtvCount = 4096;
		descriptorManagerCreateInfo.m_DsvCount = 1024;
		descriptorManagerCreateInfo.m_SamplerCount = 2048;
		m_DescriptorManager = std::make_unique<DX12DescriptorManager>(descriptorManagerCreateInfo);

		m_TransferManager = std::make_unique<TransferManager>(m_Device.get());

		m_RGTransientResourcePool = std::make_unique<RGTransientResourcePool>(m_Device.get());

		m_Device->SetDescriptorManager(m_DescriptorManager.get());

		m_PSOCache = std::make_unique<DX12PSOCache>(m_Device.get(), std::make_unique<StreamPSOCreator>());

		m_RootSignatureCache = std::make_unique<DX12RootSignatureCache>(m_Device.get());

		PipelineCache::CreateInfo pipelineCacheCreateInfo{
			.m_Device = m_Device.get(),
			.m_ShaderManager = createInfo.m_ShaderManager,
			.m_RootSignatureCache = m_RootSignatureCache.get(),
			.m_PSOCache = m_PSOCache.get(),
		};
		m_PipelineCache =
			std::make_unique<PipelineCache>(pipelineCacheCreateInfo);

		SamplerRegistry::CreateInfo samplerRegistryCreateInfo{};
		samplerRegistryCreateInfo.m_Device = m_Device.get();
		m_SamplerRegistry = std::make_unique<SamplerRegistry>(samplerRegistryCreateInfo);
		m_SamplerRegistry->InitializePresetSamplers();

		TextureRegistry::CreateInfo textureRegistryCreateInfo{};
		textureRegistryCreateInfo.m_DX12Device = m_Device.get();
		textureRegistryCreateInfo.m_TransferManager = m_TransferManager.get();
		m_TextureRegistry = std::make_unique<TextureRegistry>(textureRegistryCreateInfo);
		m_TextureRegistry->InitializeReservedTextures();

		RenderResourceRegistry::CreateInfo renderResRegistryCreateInfo{};
		renderResRegistryCreateInfo.m_Device = m_Device.get();
		renderResRegistryCreateInfo.m_TransientResourcePool = m_RGTransientResourcePool.get();
		renderResRegistryCreateInfo.m_SamplerRegistry = m_SamplerRegistry.get();
		m_RenderResRegistry = std::make_unique<RenderResourceRegistry>(renderResRegistryCreateInfo);

		m_DevelopGuiBackend = std::make_unique<DevelopGuiBackend>();
		DevelopGuiBackend::CreateInfo developGuiBackendCreateInfo{};
		developGuiBackendCreateInfo.m_Hwnd = createInfo.m_Hwnd;
		developGuiBackendCreateInfo.m_DX12Device = m_Device.get();
		developGuiBackendCreateInfo.m_BackBufferFormat = m_SwapChain->GetFormat();
		developGuiBackendCreateInfo.m_BufferCount = m_SwapChain->GetBufferCount();
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
		m_TextureRegistry->Finalize(m_LastSubmittedFencePoint.ToRHI());
		m_TextureRegistry.reset();
		m_SamplerRegistry.reset();
		m_PipelineCache.reset();
		m_RootSignatureCache.reset();
		m_PSOCache.reset();
		m_RGTransientResourcePool.reset();
		m_Device->SetDescriptorManager(nullptr);

		m_SceneCB.reset();
		m_ObjectTable.reset();
		m_MaterialTable.reset();
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

		m_SceneCB->Tick();
		m_ViewSB->Tick();

		m_RGTransientResourcePool->Tick();
		m_DescriptorManager->Tick();
		m_Device->RetireCompletedWork();

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
		auto graphicsCommandContext = m_Device->GetGraphicsCommandContext(backBufferIndex);
		auto commandQueue = m_Device->GetCommandQueue(CommandQueueType::Graphics);

		// Wait Structured Buffer upload
		if (renderContext.m_UploadFencePoint.IsValid())
		{
			m_Device->WaitForFence(RHIQueueType::Graphics, renderContext.m_UploadFencePoint);
		}

		frame.m_CommandAllocator = commandAllocatorPool->RequestCommandAllocator();
		commandList->Begin(frame.m_CommandAllocator);
		graphicsCommandContext->ClearTrackedResourceUses();
		const DX12DescriptorHeap* descriptorHeaps[] = {
			m_DescriptorManager->GetHeap(DX12DescriptorManager::HeapType::CbvSrvUav),
			m_DescriptorManager->GetHeap(DX12DescriptorManager::HeapType::Sampler),
		};
		commandList->SetDescriptorHeaps(descriptorHeaps);

		RGExecuteContext executeContext{
			RGBackendExecuteContext{
				.m_GraphicsCommandContext = graphicsCommandContext,
			}
		};
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
		auto* graphicsCommandContext = m_Device->GetGraphicsCommandContext(frame.m_BackBufferIndex);
		auto* commandQueue = m_Device->GetCommandQueue(CommandQueueType::Graphics);

		const DX12CommandList* commandLists[] = { commandList };
		m_LastSubmittedFencePoint = commandQueue->Execute(commandLists);

		for (const RHIBufferHandle buffer : graphicsCommandContext->GetUsedBuffers())
		{
			m_Device->RecordBufferUse(buffer, m_LastSubmittedFencePoint);
		}
		for (const RHITextureHandle texture : graphicsCommandContext->GetUsedTextures())
		{
			m_Device->RecordTextureUse(texture, m_LastSubmittedFencePoint);
		}
		graphicsCommandContext->ClearTrackedResourceUses();

		RetireSceneGpuAllocations(
			&frame.m_SceneGpuAllocations,
			m_LastSubmittedFencePoint);

		frame.m_RenderGraph->Retire(m_LastSubmittedFencePoint.ToRHI());

		commandAllocatorPool->RecycleCommandAllocator(
			frame.m_CommandAllocator,
			m_LastSubmittedFencePoint);

		m_DescriptorManager->EndFrame(m_LastSubmittedFencePoint);

		swapChain->SetFrameCompletionFence(m_LastSubmittedFencePoint.ToRHI());
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
		if (m_Device && frame.m_UploadFencePoint.IsValid())
		{
			m_Device->WaitForFence(RHIQueueType::Graphics, frame.m_UploadFencePoint);
		}

		if (m_Device)
		{
			m_Device->GetGraphicsCommandContext(frame.m_BackBufferIndex)->ClearTrackedResourceUses();
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
				frame.m_RenderGraph->Retire(m_LastSubmittedFencePoint.ToRHI());
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

		if (allocations->m_Views.IsValid())
		{
			m_ViewSB->Retire(&allocations->m_Views, fencePoint.ToRHI());
		}
		if (allocations->m_SceneConstants.IsValid())
		{
			m_SceneCB->Retire(&allocations->m_SceneConstants, fencePoint.ToRHI());
		}
		*allocations = {};
	}

	DX12RootSignature* Renderer::GetCommonRootSignature() const noexcept
	{
		return m_RootSignatureCache->GetDX12RootSignature(m_CommonRootSignatureId);
	}

	RHIBindingLayoutDesc Renderer::BuildCommonRHIBindingLayoutDesc() noexcept
	{
		RHIBindingLayoutDesc desc{};
		desc.m_DebugName = "RendererCommonBindingLayout";

		AddBindingSlot(desc, RHIBindingType::ConstantBuffer, RHIShaderStage::AllGraphics, 0, 0, 1, 0, "SceneCB");
		AddBindingSlot(desc, RHIBindingType::PushConstants, RHIShaderStage::AllGraphics, 1, 0, 1, MaxDrawConstantDWORDs * sizeof(uint32_t), "DrawConstants");
		AddBindingSlot(desc, RHIBindingType::PushConstants, RHIShaderStage::AllGraphics, 2, 0, 1, MaxPassConstantDWORDs * sizeof(uint32_t), "PassConstants");
		AddBindingSlot(desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::AllGraphics, 1, 0, 1, 0, "ObjectSB");
		AddBindingSlot(desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::AllGraphics, 2, 0, 1, 0, "MaterialSB");
		AddBindingSlot(desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::AllGraphics, 3, 0, 1, 0, "ViewSB");
		AddBindingSlot(desc, RHIBindingType::BindlessSampledTextureTable, RHIShaderStage::AllGraphics, 0, 0, 0, 0, "BindlessTextures");
		AddBindingSlot(desc, RHIBindingType::BindlessSamplerTable, RHIShaderStage::AllGraphics, 0, 0, 0, 0, "BindlessSamplers");
		return desc;
	}

	RenderGraph::CreateInfo Renderer::CreateRenderGraphCreateInfo() const noexcept
	{
		RenderGraph::CreateInfo rgCreateInfo{};
		rgCreateInfo.m_Device = m_Device.get();
		rgCreateInfo.m_TransientResourcePool = m_RGTransientResourcePool.get();

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

		m_SwapChain->Resize(width, height);
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
		const RHIBindingLayoutDesc commonBindingLayout = BuildCommonRHIBindingLayoutDesc();
		auto [id, rootSig] = m_RootSignatureCache->GetOrCreate(commonBindingLayout);
		m_CommonRootSignatureId = id;
	}

	void Renderer::InitializeGpuBuffers() noexcept
	{
		// Small, per-frame CPU-visible constant allocations.
		{
			DynamicConstantBufferAllocator::CreateInfo createInfo{};
			createInfo.m_Device = m_Device.get();
			createInfo.m_CapacityInBytes =
				static_cast<uint32_t>(sizeof(SceneGPU)) * m_SwapChain->GetBufferCount() * 4;
			createInfo.m_DebugName = "Renderer.DynamicConstants";
			m_SceneCB = std::make_unique<DynamicConstantBufferAllocator>(createInfo);
		}

		// Persistent scene tables are replicated per backbuffer. Each frame only
		// updates the safe physical version selected by its backbuffer index.
		{
			PersistentStructuredBuffer<ObjectGPU>::CreateInfo objectSBCreateInfo{};
			objectSBCreateInfo.m_Device = m_Device.get();
			objectSBCreateInfo.m_ElementCapacity = MaxObjectCapacity;
			objectSBCreateInfo.m_BufferCount = m_SwapChain->GetBufferCount();
			objectSBCreateInfo.m_DebugName = "Renderer.PersistentObjects";
			m_ObjectSB = std::make_unique<PersistentStructuredBuffer<ObjectGPU>>(objectSBCreateInfo);
			m_ObjectTable = std::make_unique<PersistentStructuredBufferTable<uint64_t, ObjectGPU>>(
				MaxObjectCapacity, m_SwapChain->GetBufferCount());

			PersistentStructuredBuffer<MaterialGPU>::CreateInfo materialSBCreateInfo{};
			materialSBCreateInfo.m_Device = m_Device.get();
			materialSBCreateInfo.m_ElementCapacity = MaxMaterialCapacity;
			materialSBCreateInfo.m_BufferCount = m_SwapChain->GetBufferCount();
			materialSBCreateInfo.m_DebugName = "Renderer.PersistentMaterials";
			m_MaterialSB = std::make_unique<PersistentStructuredBuffer<MaterialGPU>>(materialSBCreateInfo);
			m_MaterialTable = std::make_unique<PersistentStructuredBufferTable<MaterialID, MaterialGPU>>(
				MaxMaterialCapacity, m_SwapChain->GetBufferCount());

			// View data remains a small per-frame dynamic upload allocation.
			DynamicStructuredBufferAllocator<ViewGPU>::CreateInfo viewSBCreateInfo{};
			viewSBCreateInfo.m_Device = m_Device.get();
			viewSBCreateInfo.m_ElementCapacity =
				MaxViewCapacity * m_SwapChain->GetBufferCount();
			viewSBCreateInfo.m_DebugName = "Renderer.DynamicViews";
			m_ViewSB = std::make_unique<DynamicStructuredBufferAllocator<ViewGPU>>(viewSBCreateInfo);
		}
	}
}
