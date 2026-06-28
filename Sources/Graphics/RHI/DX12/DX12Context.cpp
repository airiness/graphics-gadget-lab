#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12Context.h"
#include "Graphics/RHI/DX12/DX12CommandAllocator.h"
#include "Graphics/RHI/DX12/DX12CommandContext.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12QueueSystem.h"
#include "Graphics/RHI/DX12/DX12PipelineSystem.h"
#include "Graphics/RHI/DX12/DX12SwapChain.h"
#include "Graphics/RHI/DX12/DX12TransferContext.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/RHI/DX12/Utility/DX12FormatUtils.h"
#include "Graphics/TransferManager.h"

namespace gglab
{
	DX12FrameContext::DX12FrameContext(DX12Context* context, uint32_t frameIndex) noexcept :
		m_Context(context),
		m_FrameIndex(frameIndex),
		m_BackBufferIndex(frameIndex)
	{
		GGLAB_ASSERT_NOT_NULL(m_Context);
	}

	RHITextureHandle DX12FrameContext::GetBackBuffer() const noexcept
	{
		return m_Context->GetSwapChain().GetBackBufferHandle(m_BackBufferIndex);
	}

	RHIGraphicsCommandContext& DX12FrameContext::GetGraphicsContext() noexcept
	{
		GGLAB_ASSERT_MSG(m_Active, "DX12FrameContext::GetGraphicsContext requires an active frame.");
		return m_Context->GetQueueSystem().GetGraphicsContext(m_FrameIndex);
	}

	RHIComputeCommandContext* DX12FrameContext::GetComputeContext() noexcept
	{
		GGLAB_ASSERT_MSG(m_Active, "DX12FrameContext::GetComputeContext requires an active frame.");
		return m_Context->AcquireComputeContext(*this);
	}

	DX12Context::DX12Context(const RHIContextDesc& desc) noexcept
	{
		GGLAB_ASSERT_MSG(desc.m_WindowHandle != nullptr, "DX12Context requires a window handle.");
		GGLAB_ASSERT_MSG(desc.m_Width > 0 && desc.m_Height > 0, "DX12Context requires a valid extent.");
		m_Device = std::make_unique<DX12Device>();
		m_Device->Initialize();
		m_PipelineSystem = std::make_unique<DX12PipelineSystem>(m_Device.get());
		DX12QueueSystem::CreateInfo queueSystemInfo{};
		queueSystemInfo.m_Device = m_Device.get();
		queueSystemInfo.m_FrameCount = desc.m_BufferCount;
		queueSystemInfo.m_PipelineSystem = static_cast<DX12PipelineSystem*>(m_PipelineSystem.get());
		m_QueueSystem = std::make_unique<DX12QueueSystem>(queueSystemInfo);
		m_Device->SetQueueSystem(m_QueueSystem.get());

		DX12DescriptorManager::CreateInfo descriptorInfo{};
		descriptorInfo.m_DX12Device = m_Device.get();
		m_DescriptorManager = std::make_unique<DX12DescriptorManager>(descriptorInfo);
		m_Device->SetDescriptorManager(m_DescriptorManager.get());

		DX12SwapChain::CreateInfo swapChainInfo{};
		swapChainInfo.m_DX12Device = m_Device.get();
		swapChainInfo.m_QueueSystem = m_QueueSystem.get();
		swapChainInfo.m_PresentQueue = &m_QueueSystem->GetQueue(DX12QueueType::Graphics);
		swapChainInfo.m_Hwnd = static_cast<HWND>(desc.m_WindowHandle);
		swapChainInfo.m_Width = desc.m_Width;
		swapChainInfo.m_Height = desc.m_Height;
		swapChainInfo.m_Format = ToDXGIFormat(desc.m_BackBufferFormat);
		swapChainInfo.m_BufferCount = desc.m_BufferCount;
		swapChainInfo.m_AllowTearing = desc.m_AllowTearing && m_Device->SupportTearing();
		swapChainInfo.m_Vsync = desc.m_Vsync;
		auto swapChain = std::make_unique<DX12SwapChain>();
		GGLAB_ASSERT_MSG(swapChain->Initialize(swapChainInfo),
			"DX12Context failed to initialize the swapchain.");
		m_SwapChain = std::move(swapChain);
		GGLAB_ASSERT_MSG(m_SwapChain && m_SwapChain->IsValid(),
			"DX12Context failed to create the swapchain.");

		m_TransferManager = std::make_unique<TransferManager>(
			std::make_unique<DX12TransferContext>(m_Device.get(), m_QueueSystem.get()));
		m_Frames.reserve(desc.m_BufferCount);
		for (uint32_t i = 0; i < desc.m_BufferCount; ++i)
		{
			m_Frames.push_back(std::make_unique<DX12FrameContext>(this, i));
		}
		m_Initialized = true;
	}

	DX12Context::~DX12Context()
	{
		Finalize();
	}

	RHIDevice& DX12Context::GetDevice() noexcept { return *m_Device; }
	const RHIDevice& DX12Context::GetDevice() const noexcept { return *m_Device; }
	RHISwapChain& DX12Context::GetSwapChain() noexcept { return *m_SwapChain; }
	const RHISwapChain& DX12Context::GetSwapChain() const noexcept { return *m_SwapChain; }
	TransferManager& DX12Context::GetTransferManager() noexcept { return *m_TransferManager; }
	RHIPipelineSystem& DX12Context::GetPipelineSystem() noexcept { return *m_PipelineSystem; }
	DX12Device& DX12Context::GetDX12Device() noexcept { return *m_Device; }
	const DX12Device& DX12Context::GetDX12Device() const noexcept { return *m_Device; }
	DX12DescriptorManager& DX12Context::GetDescriptorManager() noexcept { return *m_DescriptorManager; }
	DX12QueueSystem& DX12Context::GetQueueSystem() noexcept { return *m_QueueSystem; }
	const DX12QueueSystem& DX12Context::GetQueueSystem() const noexcept { return *m_QueueSystem; }

	RHIFrameContext& DX12Context::BeginFrame() noexcept
	{
		GGLAB_ASSERT_MSG(m_Initialized, "DX12Context::BeginFrame called after finalization.");
		GGLAB_ASSERT_MSG(m_ActiveFrame == nullptr, "DX12Context only supports one active frame.");

		m_SwapChain->WaitFrameCompletion();
		RetireCompletedWork();

		const uint32_t frameIndex = m_SwapChain->GetCurrentBackBufferIndex();
		GGLAB_ASSERT(frameIndex < m_Frames.size());
		auto& frame = *m_Frames[frameIndex];
		GGLAB_ASSERT_MSG(!frame.m_Active, "DX12 frame slot is already active.");
		frame.m_BackBufferIndex = frameIndex;
		frame.m_SubmittedFence = {};
		frame.m_ComputeAllocator = nullptr;
		frame.m_Active = true;
		m_ActiveFrame = &frame;
		BeginGraphicsRecording(frame);
		return frame;
	}

	RHIFencePoint DX12Context::EndFrame(RHIFrameContext& rhiFrame) noexcept
	{
		auto* frame = dynamic_cast<DX12FrameContext*>(&rhiFrame);
		GGLAB_ASSERT_MSG(frame && frame == m_ActiveFrame && frame->m_Active,
			"DX12Context::EndFrame received a foreign or inactive frame.");
		if (!frame || frame != m_ActiveFrame || !frame->m_Active)
		{
			return {};
		}

		DX12FencePoint computeFence{};
		if (frame->m_ComputeAllocator)
		{
			auto* computeList = &m_QueueSystem->GetComputeCommandList(frame->m_FrameIndex);
			auto* computeContext = &m_QueueSystem->GetComputeContext(frame->m_FrameIndex);
			auto* computeQueue = &m_QueueSystem->GetQueue(DX12QueueType::Compute);
			computeList->End();
			const DX12CommandList* lists[] = { computeList };
			computeFence = computeQueue->Execute(lists);
			for (RHIBufferHandle buffer : computeContext->GetUsedBuffers())
			{
				m_Device->RecordBufferUse(buffer, computeFence);
			}
			for (RHITextureHandle texture : computeContext->GetUsedTextures())
			{
				m_Device->RecordTextureUse(texture, computeFence);
			}
			computeContext->ClearTrackedResourceUses();
			m_QueueSystem->GetAllocatorPool(DX12QueueType::Compute).RecycleCommandAllocator(
				frame->m_ComputeAllocator, computeFence);
			m_QueueSystem->WaitForFence(RHIQueueType::Graphics, computeFence.ToRHI());
		}

		auto* graphicsList = &m_QueueSystem->GetGraphicsCommandList(frame->m_FrameIndex);
		auto* graphicsContext = &m_QueueSystem->GetGraphicsContext(frame->m_FrameIndex);
		auto* graphicsQueue = &m_QueueSystem->GetQueue(DX12QueueType::Graphics);
		graphicsList->End();
		const DX12CommandList* lists[] = { graphicsList };
		const DX12FencePoint graphicsFence = graphicsQueue->Execute(lists);
		for (RHIBufferHandle buffer : graphicsContext->GetUsedBuffers())
		{
			m_Device->RecordBufferUse(buffer, graphicsFence);
		}
		for (RHITextureHandle texture : graphicsContext->GetUsedTextures())
		{
			m_Device->RecordTextureUse(texture, graphicsFence);
		}
		graphicsContext->ClearTrackedResourceUses();
		m_QueueSystem->GetAllocatorPool(DX12QueueType::Graphics).RecycleCommandAllocator(
			frame->m_GraphicsAllocator, graphicsFence);

		m_DescriptorManager->EndFrame(graphicsFence);
		m_SwapChain->SetFrameCompletionFence(graphicsFence.ToRHI());
		m_SwapChain->Present();
		FinishFrame(*frame, graphicsFence.ToRHI());
		return graphicsFence.ToRHI();
	}

	void DX12Context::AbortFrame(RHIFrameContext& rhiFrame) noexcept
	{
		auto* frame = dynamic_cast<DX12FrameContext*>(&rhiFrame);
		GGLAB_ASSERT_MSG(frame && frame == m_ActiveFrame && frame->m_Active,
			"DX12Context::AbortFrame received a foreign or inactive frame.");
		if (!frame || frame != m_ActiveFrame || !frame->m_Active)
		{
			return;
		}

		if (frame->m_ComputeAllocator)
		{
			auto* computeList = &m_QueueSystem->GetComputeCommandList(frame->m_FrameIndex);
			auto* computeContext = &m_QueueSystem->GetComputeContext(frame->m_FrameIndex);
			computeList->End();
			computeContext->ClearTrackedResourceUses();
			const DX12FencePoint fence = m_QueueSystem->GetQueue(DX12QueueType::Compute).Signal();
			m_QueueSystem->GetAllocatorPool(DX12QueueType::Compute).RecycleCommandAllocator(
				frame->m_ComputeAllocator, fence);
		}

		auto* graphicsList = &m_QueueSystem->GetGraphicsCommandList(frame->m_FrameIndex);
		auto* graphicsContext = &m_QueueSystem->GetGraphicsContext(frame->m_FrameIndex);
		graphicsList->End();
		graphicsContext->ClearTrackedResourceUses();
		const DX12FencePoint fence = m_QueueSystem->GetQueue(DX12QueueType::Graphics).Signal();
		m_QueueSystem->GetAllocatorPool(DX12QueueType::Graphics).RecycleCommandAllocator(
			frame->m_GraphicsAllocator, fence);
		m_DescriptorManager->EndFrame(fence);
		FinishFrame(*frame, fence.ToRHI());
	}

	void DX12Context::WaitForFence(
		RHIQueueType waitingQueue,
		const RHIFencePoint& fencePoint) noexcept
	{
		m_QueueSystem->WaitForFence(waitingQueue, fencePoint);
	}

	void DX12Context::Resize(uint32_t width, uint32_t height) noexcept
	{
		GGLAB_ASSERT_MSG(m_ActiveFrame == nullptr, "DX12Context::Resize cannot run during a frame.");
		if (width == 0 || height == 0 || !m_SwapChain || !m_SwapChain->IsValid())
		{
			return;
		}
		WaitIdle();
		m_SwapChain->Resize(width, height);
	}

	void DX12Context::WaitIdle() noexcept
	{
		if (m_QueueSystem)
		{
			m_QueueSystem->WaitIdle();
		}
	}

	void DX12Context::RetireCompletedWork() noexcept
	{
		m_TransferManager->Reclaim();
		m_DescriptorManager->Tick();
		m_Device->RetireCompletedWork();
	}

	uint32_t DX12Context::GetCurrentFrameIndex() const noexcept
	{
		return m_SwapChain->GetCurrentBackBufferIndex();
	}

	DX12ComputeCommandContext* DX12Context::AcquireComputeContext(DX12FrameContext& frame) noexcept
	{
		GGLAB_ASSERT_MSG(&frame == m_ActiveFrame && frame.m_Active,
			"Compute context can only be acquired from the active frame.");
		auto* context = &m_QueueSystem->GetComputeContext(frame.m_FrameIndex);
		if (!frame.m_ComputeAllocator)
		{
			frame.m_ComputeAllocator =
				m_QueueSystem->GetAllocatorPool(DX12QueueType::Compute).RequestCommandAllocator();
			auto* commandList = &m_QueueSystem->GetComputeCommandList(frame.m_FrameIndex);
			commandList->Begin(frame.m_ComputeAllocator);
			context->ClearTrackedResourceUses();
			BindGlobalDescriptorHeaps(*commandList);
		}
		return context;
	}

	void DX12Context::BeginGraphicsRecording(DX12FrameContext& frame) noexcept
	{
		frame.m_GraphicsAllocator =
			m_QueueSystem->GetAllocatorPool(DX12QueueType::Graphics).RequestCommandAllocator();
		auto* commandList = &m_QueueSystem->GetGraphicsCommandList(frame.m_FrameIndex);
		commandList->Begin(frame.m_GraphicsAllocator);
		m_QueueSystem->GetGraphicsContext(frame.m_FrameIndex).ClearTrackedResourceUses();
		BindGlobalDescriptorHeaps(*commandList);
	}

	void DX12Context::BindGlobalDescriptorHeaps(DX12CommandList& commandList) noexcept
	{
		const DX12DescriptorHeap* heaps[] = {
			m_DescriptorManager->GetHeap(DX12DescriptorManager::HeapType::CbvSrvUav),
			m_DescriptorManager->GetHeap(DX12DescriptorManager::HeapType::Sampler),
		};
		commandList.SetDescriptorHeaps(heaps);
	}

	void DX12Context::FinishFrame(DX12FrameContext& frame, const RHIFencePoint& fencePoint) noexcept
	{
		frame.m_SubmittedFence = fencePoint;
		frame.m_GraphicsAllocator = nullptr;
		frame.m_ComputeAllocator = nullptr;
		frame.m_Active = false;
		m_ActiveFrame = nullptr;
	}

	void DX12Context::Finalize() noexcept
	{
		if (!m_Initialized)
		{
			return;
		}
		if (m_ActiveFrame)
		{
			AbortFrame(*m_ActiveFrame);
		}
		WaitIdle();
		m_Frames.clear();
		m_TransferManager.reset();
		if (m_SwapChain)
		{
			m_SwapChain->Finalize();
			m_SwapChain.reset();
		}
		m_Device->SetDescriptorManager(nullptr);
		m_DescriptorManager->Tick();
		m_DescriptorManager.reset();
		m_PipelineSystem.reset();
		m_Device->RetireCompletedWork();
		m_Device->Finalize();
		m_QueueSystem->Finalize();
		m_Device->SetQueueSystem(nullptr);
		m_QueueSystem.reset();
		m_Device.reset();
		m_Initialized = false;
	}
}
