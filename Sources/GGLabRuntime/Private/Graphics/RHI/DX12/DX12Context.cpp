#include "Graphics/RHI/DX12/DX12Context.h"
#include "GGLabRuntime/Graphics/RHI/DX12/DX12ContextFactory.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/RHI/DX12/DX12CommandAllocator.h"
#include "Graphics/RHI/DX12/DX12CommandContext.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12Fence.h"
#include "Graphics/RHI/DX12/DX12QueueSystem.h"
#include "Graphics/RHI/DX12/DX12PipelineSystem.h"
#include "Graphics/RHI/DX12/DX12SwapChain.h"
#include "Graphics/RHI/DX12/DX12TransferContext.h"
#include "Graphics/RHI/DX12/DX12GpuProfiler.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/Utility/DXGIFormatUtils.h"
#include "GGLabRuntime/Graphics/TransferManager.h"

#include <array>
#include <memory>
#include <span>
#include <utility>

namespace gglab
{
	std::unique_ptr<RHIContext> CreateDX12Context(const RHIContextDesc& desc, HWND window) noexcept
	{
		if (!window)
		{
			return {};
		}
		return std::make_unique<DX12Context>(desc, window);
	}

	DX12FrameContext::DX12FrameContext(DX12Context* context, uint32_t frameSlotIndex) noexcept :
		m_Context(context), m_FrameSlotIndex(frameSlotIndex)
	{
		GGLAB_ASSERT_NOT_NULL(m_Context);
	}

	RHITextureHandle DX12FrameContext::GetBackBuffer() const noexcept
	{
		return m_Context->GetSwapChain().GetBackBufferHandle(m_BackBufferIndex);
	}

	RHIGraphicsCommandContext& DX12FrameContext::GetGraphicsContext() noexcept
	{
		GGLAB_ASSERT_MSG(
			m_Active, "DX12FrameContext::GetGraphicsContext requires an active frame.");
		return m_Context->GetQueueSystem().GetGraphicsContext(m_FrameSlotIndex);
	}

	RHIComputeCommandContext& DX12FrameContext::GetDirectComputeContext() noexcept
	{
		GGLAB_ASSERT_MSG(
			m_Active, "DX12FrameContext::GetDirectComputeContext requires an active frame.");
		return m_Context->GetDirectComputeContext(*this);
	}

	RHIComputeCommandContext* DX12FrameContext::GetComputeContext() noexcept
	{
		GGLAB_ASSERT_MSG(m_Active, "DX12FrameContext::GetComputeContext requires an active frame.");
		return m_Context->AcquireComputeContext(*this);
	}

	RHIFrameUploads DX12FrameContext::GetUploads() noexcept
	{
		GGLAB_ASSERT_MSG(m_Active, "DX12FrameContext::GetUploads requires an active frame.");
		return m_UploadWriter.GetFrameUploads();
	}

	DX12Context::DX12Context(const RHIContextDesc& desc, HWND window) noexcept
	{
		GGLAB_ASSERT_MSG(window != nullptr, "DX12Context requires a window handle.");
		GGLAB_ASSERT_MSG(
			desc.m_Width > 0 && desc.m_Height > 0, "DX12Context requires a valid extent.");
		GGLAB_ASSERT_MSG(desc.m_FrameSlotCount >= 2,
			"DX12Context currently requires at least two frame slots for its swapchain.");
		m_Device = std::make_unique<DX12Device>();
		m_Device->Initialize({
			.m_EnableDebugValidation = desc.m_EnableDebugValidation,
			.m_EnableGpuBasedValidation = desc.m_EnableGpuBasedValidation,
			});
		m_UploadPool = std::make_unique<RHIUploadPool>(*m_Device);
		m_PipelineSystem = std::make_unique<DX12PipelineSystem>(m_Device.get());
		DX12QueueSystem::CreateInfo queueSystemInfo{};
		queueSystemInfo.m_Device = m_Device.get();
		queueSystemInfo.m_FrameCount = desc.m_FrameSlotCount;
		queueSystemInfo.m_PipelineSystem = static_cast<DX12PipelineSystem*>(m_PipelineSystem.get());
		m_QueueSystem = std::make_unique<DX12QueueSystem>(queueSystemInfo);
		m_Device->SetQueueSystem(m_QueueSystem.get());
		m_GpuProfiler = std::make_unique<DX12GpuProfiler>(m_Device.get(),
			&m_QueueSystem->GetQueue(DX12QueueType::Graphics), desc.m_FrameSlotCount);
		m_DirectComputeContexts.reserve(desc.m_FrameSlotCount);
		for (uint32_t frameSlotIndex = 0; frameSlotIndex < desc.m_FrameSlotCount; ++frameSlotIndex)
		{
			auto& graphicsContext = m_QueueSystem->GetGraphicsContext(frameSlotIndex);
			graphicsContext.SetGpuProfiler(m_GpuProfiler.get());
			auto computeContext = std::make_unique<DX12ComputeCommandContext>(
				graphicsContext, static_cast<DX12PipelineSystem*>(m_PipelineSystem.get()));
			computeContext->SetGpuProfiler(m_GpuProfiler.get());
			GGLAB_ASSERT_MSG(
				computeContext->GetHandle() == graphicsContext.GetHandle() &&
				computeContext->GetQueueType() == RHIQueueType::Graphics &&
				computeContext->GetCommandList() == graphicsContext.GetCommandList(),
				"A direct compute encoder must share its graphics context handle, queue, and command list.");
			m_DirectComputeContexts.push_back(std::move(computeContext));
		}

		DX12DescriptorManager::CreateInfo descriptorInfo{};
		descriptorInfo.m_DX12Device = m_Device.get();
		m_DescriptorManager = std::make_unique<DX12DescriptorManager>(descriptorInfo);
		m_Device->SetDescriptorManager(m_DescriptorManager.get());

		DX12SwapChain::CreateInfo swapChainInfo{};
		swapChainInfo.m_DX12Device = m_Device.get();
		swapChainInfo.m_PresentQueue = &m_QueueSystem->GetQueue(DX12QueueType::Graphics);
		swapChainInfo.m_Hwnd = window;
		swapChainInfo.m_Width = desc.m_Width;
		swapChainInfo.m_Height = desc.m_Height;
		swapChainInfo.m_Format = ToDXGIFormat(desc.m_BackBufferFormat);
		// DX12 currently requests one swapchain image per frame slot. The counts remain
		// semantically independent so another backend may choose a surface-compatible count.
		swapChainInfo.m_BufferCount = desc.m_FrameSlotCount;
		swapChainInfo.m_AllowTearing = desc.m_AllowTearing && m_Device->SupportTearing();
		swapChainInfo.m_Vsync = desc.m_Vsync;
		auto swapChain = std::make_unique<DX12SwapChain>();
		const auto swapChainInitResult = swapChain->Initialize(swapChainInfo);
		GGLAB_ASSERT_MSG(swapChainInitResult, "DX12Context failed to initialize the swapchain.");
		GGLAB_UNUSED(swapChainInitResult);
		m_SwapChain = std::move(swapChain);
		GGLAB_ASSERT_MSG(
			m_SwapChain && m_SwapChain->IsValid(), "DX12Context failed to create the swapchain.");
		m_BackBufferCompletionFences.resize(m_SwapChain->GetBufferCount());

		m_TransferManager = std::make_unique<TransferManager>(
			std::make_unique<DX12TransferContext>(m_Device.get(), m_QueueSystem.get()));
		m_Frames.reserve(desc.m_FrameSlotCount);
		for (uint32_t i = 0; i < desc.m_FrameSlotCount; ++i)
		{
			m_Frames.push_back(std::make_unique<DX12FrameContext>(this, i));
		}
		m_Initialized = true;
	}

	DX12Context::~DX12Context()
	{
		Finalize();
	}

	RHIDevice& DX12Context::GetDevice() noexcept
	{
		return *m_Device;
	}
	const RHIDevice& DX12Context::GetDevice() const noexcept
	{
		return *m_Device;
	}
	RHISwapChain& DX12Context::GetSwapChain() noexcept
	{
		return *m_SwapChain;
	}
	const RHISwapChain& DX12Context::GetSwapChain() const noexcept
	{
		return *m_SwapChain;
	}
	TransferManager& DX12Context::GetTransferManager() noexcept
	{
		return *m_TransferManager;
	}
	RHIPipelineSystem& DX12Context::GetPipelineSystem() noexcept
	{
		return *m_PipelineSystem;
	}
	GpuProfiler* DX12Context::GetGpuProfiler() noexcept
	{
		return m_GpuProfiler.get();
	}
	const GpuProfiler* DX12Context::GetGpuProfiler() const noexcept
	{
		return m_GpuProfiler.get();
	}
	DX12Device& DX12Context::GetDX12Device() noexcept
	{
		return *m_Device;
	}
	const DX12Device& DX12Context::GetDX12Device() const noexcept
	{
		return *m_Device;
	}
	DX12DescriptorManager& DX12Context::GetDescriptorManager() noexcept
	{
		return *m_DescriptorManager;
	}
	DX12QueueSystem& DX12Context::GetQueueSystem() noexcept
	{
		return *m_QueueSystem;
	}
	const DX12QueueSystem& DX12Context::GetQueueSystem() const noexcept
	{
		return *m_QueueSystem;
	}

	RHIFrameBeginResult DX12Context::BeginFrame() noexcept
	{
		GGLAB_ASSERT_MSG(m_Initialized, "DX12Context::BeginFrame called after finalization.");
		GGLAB_ASSERT_MSG(m_ActiveFrame == nullptr, "DX12Context only supports one active frame.");
		if (!m_Initialized || m_ActiveFrame != nullptr || !m_SwapChain || !m_SwapChain->IsValid())
		{
			return RHIFrameBeginResult::Fatal();
		}
		if (IsPoisoned() || IsDeviceLost())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DX12Context refuses new frames after a GPU completion failure.");
			return RHIFrameBeginResult::Fatal();
		}

		const uint32_t backBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
		GGLAB_ASSERT(backBufferIndex < m_BackBufferCompletionFences.size());
		if (backBufferIndex >= m_BackBufferCompletionFences.size())
		{
			return RHIFrameBeginResult::Fatal();
		}
		const RHIFencePoint& backBufferFence = m_BackBufferCompletionFences[backBufferIndex];
		if (backBufferFence.IsValid() &&
			m_QueueSystem->WaitForFenceCompletion(backBufferFence) != RHIGpuWaitResult::Completed)
		{
			GGLAB_LOG_GRAPHICS_ERROR("DX12Context could not verify back-buffer completion.");
			return RHIFrameBeginResult::Fatal();
		}

		GGLAB_ASSERT(m_NextFrameSlotIndex < m_Frames.size());
		auto& frame = *m_Frames[m_NextFrameSlotIndex];
		GGLAB_ASSERT_MSG(!frame.m_Active, "DX12 frame slot is already active.");
		if (frame.m_SubmittedFence.IsValid() &&
			m_QueueSystem->WaitForFenceCompletion(frame.m_SubmittedFence) !=
				RHIGpuWaitResult::Completed)
		{
			GGLAB_LOG_GRAPHICS_ERROR("DX12Context could not verify frame-slot completion.");
			return RHIFrameBeginResult::Fatal();
		}
		RetireCompletedWork();
		if (IsPoisoned() || IsDeviceLost())
		{
			return RHIFrameBeginResult::Fatal();
		}

		frame.m_BackBufferIndex = backBufferIndex;
		GGLAB_ASSERT(frame.m_BackBufferIndex < m_SwapChain->GetBufferCount());
		frame.m_SubmittedFence = {};
		frame.m_ComputeAllocator = nullptr;
		frame.m_Active = true;
		m_ActiveFrame = &frame;
		m_NextFrameSlotIndex = (m_NextFrameSlotIndex + 1) % GetFrameSlotCount();
		frame.m_Uploads = m_UploadPool->BeginBatch();
		frame.m_Uploads->Attach(frame.m_UploadWriter);
		BeginGraphicsRecording(frame);
		m_GpuProfiler->BeginFrame(frame.m_FrameSlotIndex,
			m_QueueSystem->GetGraphicsCommandList(frame.m_FrameSlotIndex));
		return RHIFrameBeginResult::Ready(frame);
	}

	RHIFrameEndResult DX12Context::EndFrame(RHIFrameContext& rhiFrame) noexcept
	{
		auto* frame = dynamic_cast<DX12FrameContext*>(&rhiFrame);
		GGLAB_ASSERT_MSG(frame && frame == m_ActiveFrame && frame->m_Active,
			"DX12Context::EndFrame received a foreign or inactive frame.");
		if (!frame || frame != m_ActiveFrame || !frame->m_Active)
		{
			return RHIFrameEndResult::Fatal();
		}

		if (IsPoisoned() || IsDeviceLost() ||
			m_QueueSystem->GetGraphicsContext(frame->m_FrameSlotIndex).HasRecordingError() ||
			m_DirectComputeContexts[frame->m_FrameSlotIndex]->HasRecordingError() ||
			(frame->m_ComputeAllocator &&
				m_QueueSystem->GetComputeContext(frame->m_FrameSlotIndex).HasRecordingError()) ||
			!frame->m_Uploads->Publish())
		{
			GGLAB_LOG_GRAPHICS_ERROR("DX12 discarded a frame with failed constant recording.");
			return AbortFrame(rhiFrame);
		}

		// Allocator, descriptor and resource retirement follows each queue's requested point.
		// It completes only after a native signal reached it or the device was removed, so a
		// failed signal retains them instead of releasing them early.
		std::array<RHISubmittedStream, 2> streams{};
		size_t streamCount = 0;
		DX12FencePoint computeRetentionFence{};
		if (frame->m_ComputeAllocator)
		{
			auto* computeList = &m_QueueSystem->GetComputeCommandList(frame->m_FrameSlotIndex);
			auto* computeContext = &m_QueueSystem->GetComputeContext(frame->m_FrameSlotIndex);
			auto* computeQueue = &m_QueueSystem->GetQueue(DX12QueueType::Compute);
			computeList->End();
			const DX12CommandList* lists[] = { computeList };
			const DX12SignalResult compute = computeQueue->ExecuteChecked(lists);
			computeRetentionFence = compute.m_FencePoint;
			for (RHIBufferHandle buffer : computeContext->GetUsedBuffers())
			{
				m_Device->RecordBufferUse(buffer, compute.m_FencePoint);
			}
			for (RHITextureHandle texture : computeContext->GetUsedTextures())
			{
				m_Device->RecordTextureUse(texture, compute.m_FencePoint);
			}
			computeContext->ClearTrackedResourceUses();
			m_QueueSystem->GetAllocatorPool(DX12QueueType::Compute)
				.RecycleCommandAllocator(frame->m_ComputeAllocator, compute.m_FencePoint);
			streams[streamCount++] = {
				RHISubmitEffect::Accepted, compute.GetCheckedFencePoint().ToRHI(), false
			};
		}

		auto* graphicsList = &m_QueueSystem->GetGraphicsCommandList(frame->m_FrameSlotIndex);
		auto* graphicsContext = &m_QueueSystem->GetGraphicsContext(frame->m_FrameSlotIndex);
		auto* graphicsQueue = &m_QueueSystem->GetQueue(DX12QueueType::Graphics);
		DX12SignalResult graphics{};
		const auto submitGraphics = [&]() -> RHISubmittedStream
			{
				m_GpuProfiler->EndFrame(frame->m_FrameSlotIndex, *graphicsList);
				graphicsList->End();
				const DX12CommandList* lists[] = { graphicsList };
				graphics = graphicsQueue->ExecuteChecked(lists);
				// ExecuteCommandLists reports no failure, so an executed stream is accepted.
				return { RHISubmitEffect::Accepted, graphics.GetCheckedFencePoint().ToRHI(), false };
			};
		const RHISubmittedStream graphicsStream = streamCount == 0 ?
			submitGraphics() :
			SubmitDependentFrameStream(streams[0],
				[this](const RHIFencePoint& fence)
				{
					return m_QueueSystem->WaitForFence(RHIQueueType::Graphics, fence);
				}, submitGraphics);
		if (graphicsStream.m_Effect == RHISubmitEffect::NoEffect)
		{
			// No later CPU wait can repair a missing GPU dependency. Discard graphics before
			// ExecuteCommandLists, but retain everything the already accepted compute can read.
			GGLAB_LOG_GRAPHICS_ERROR("DX12 discarded graphics after an unverified compute join.");
			if (graphicsContext->IsRendering())
			{
				graphicsContext->EndRendering();
			}
			m_GpuProfiler->AbortFrame(frame->m_FrameSlotIndex);
			graphicsList->End();
			graphicsContext->ClearTrackedResourceUses();
			m_QueueSystem->GetAllocatorPool(DX12QueueType::Graphics)
				.RecycleCommandAllocator(frame->m_GraphicsAllocator, computeRetentionFence);
			// Keep descriptor frees pending until verified teardown. A compute fence does not
			// cover earlier graphics users, and this poisoned context cannot submit another frame.
			const RHISubmissionResolution resolution = ResolveFrameSubmission(
				std::span(streams.data(), streamCount), *frame->m_Uploads,
				MakeSubmissionVerification());
			m_Poisoned = true;
			FinishFrame(*frame, resolution.m_Fence);
			return resolution.m_State == RHIFrameSubmissionState::CompletionUnknown ?
				RHIFrameEndResult::FatalCompletionUnknown() :
				RHIFrameEndResult::Fatal(resolution.m_Fence);
		}
		for (RHIBufferHandle buffer : graphicsContext->GetUsedBuffers())
		{
			m_Device->RecordBufferUse(buffer, graphics.m_FencePoint);
		}
		for (RHITextureHandle texture : graphicsContext->GetUsedTextures())
		{
			m_Device->RecordTextureUse(texture, graphics.m_FencePoint);
		}
		graphicsContext->ClearTrackedResourceUses();
		m_QueueSystem->GetAllocatorPool(DX12QueueType::Graphics)
			.RecycleCommandAllocator(frame->m_GraphicsAllocator, graphics.m_FencePoint);
		m_DescriptorManager->EndFrame(graphics.m_FencePoint);
		streams[streamCount++] = graphicsStream;

		const RHISubmissionResolution resolution = ResolveFrameSubmission(
			std::span(streams.data(), streamCount), *frame->m_Uploads,
			MakeSubmissionVerification());
		const RHIFencePoint retentionFence = graphics.m_FencePoint.ToRHI();
		GGLAB_ASSERT(frame->m_BackBufferIndex < m_BackBufferCompletionFences.size());
		if (frame->m_BackBufferIndex < m_BackBufferCompletionFences.size())
		{
			m_BackBufferCompletionFences[frame->m_BackBufferIndex] = retentionFence;
		}
		switch (resolution.m_State)
		{
		case RHIFrameSubmissionState::Fenced:
			break;
		case RHIFrameSubmissionState::NoOutstandingWork:
			FinishFrame(*frame, {});
			return RHIFrameEndResult::Fatal();
		case RHIFrameSubmissionState::CompletionUnknown:
			m_Poisoned = true;
			FinishFrame(*frame, retentionFence);
			return RHIFrameEndResult::FatalCompletionUnknown();
		}

		const RHIFencePoint submittedFence = resolution.m_Fence;
		const bool presented = m_SwapChain->Present();
		if (presented && !m_CompletedProductionFrame)
		{
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(
				"DX12 completed its first production submit/present frame transaction.");
			m_CompletedProductionFrame = true;
		}
		FinishFrame(*frame, submittedFence);
		return presented ?
			RHIFrameEndResult::Completed(submittedFence) :
			RHIFrameEndResult::Fatal(submittedFence);
	}

	RHIFrameEndResult DX12Context::AbortFrame(RHIFrameContext& rhiFrame) noexcept
	{
		auto* frame = dynamic_cast<DX12FrameContext*>(&rhiFrame);
		GGLAB_ASSERT_MSG(frame && frame == m_ActiveFrame && frame->m_Active,
			"DX12Context::AbortFrame received a foreign or inactive frame.");
		if (!frame || frame != m_ActiveFrame || !frame->m_Active)
		{
			return RHIFrameEndResult::Fatal();
		}

		// The frame's lists are closed but never executed. Signals only give the allocator
		// pools a retirement point; a failed signal leaves nothing outstanding either.
		if (frame->m_ComputeAllocator)
		{
			auto* computeList = &m_QueueSystem->GetComputeCommandList(frame->m_FrameSlotIndex);
			auto* computeContext = &m_QueueSystem->GetComputeContext(frame->m_FrameSlotIndex);
			computeList->End();
			computeContext->ClearTrackedResourceUses();
			const DX12SignalResult fence =
				m_QueueSystem->GetQueue(DX12QueueType::Compute).SignalChecked();
			m_QueueSystem->GetAllocatorPool(DX12QueueType::Compute)
				.RecycleCommandAllocator(frame->m_ComputeAllocator, fence.m_FencePoint);
		}

		auto* graphicsList = &m_QueueSystem->GetGraphicsCommandList(frame->m_FrameSlotIndex);
		auto* graphicsContext = &m_QueueSystem->GetGraphicsContext(frame->m_FrameSlotIndex);
		if (graphicsContext->IsRendering())
		{
			graphicsContext->EndRendering();
		}
		m_GpuProfiler->AbortFrame(frame->m_FrameSlotIndex);
		graphicsList->End();
		graphicsContext->ClearTrackedResourceUses();
		const DX12SignalResult fence =
			m_QueueSystem->GetQueue(DX12QueueType::Graphics).SignalChecked();
		m_QueueSystem->GetAllocatorPool(DX12QueueType::Graphics)
			.RecycleCommandAllocator(frame->m_GraphicsAllocator, fence.m_FencePoint);
		m_DescriptorManager->EndFrame(fence.m_FencePoint);
		frame->m_Uploads->Cancel();
		const RHIFencePoint submittedFence = fence.GetCheckedFencePoint().ToRHI();
		FinishFrame(*frame, submittedFence);
		return RHIFrameEndResult::Fatal(submittedFence);
	}

	bool DX12Context::WaitForFence(
		RHIQueueType waitingQueue, const RHIFencePoint& fencePoint) noexcept
	{
		return m_QueueSystem->WaitForFence(waitingQueue, fencePoint);
	}

	void DX12Context::Resize(uint32_t width, uint32_t height) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_ActiveFrame == nullptr, "DX12Context::Resize cannot run during a frame.");
		if (width == 0 || height == 0 || !m_SwapChain || !m_SwapChain->IsValid())
		{
			return;
		}
		if (IsPoisoned() || IsDeviceLost())
		{
			return;
		}
		const RHIGpuWaitResult idle = WaitIdleFor(RHIFrameVerificationTimeoutMilliseconds);
		if (idle != RHIGpuWaitResult::Completed)
		{
			m_Poisoned = true;
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"DX12 skipped swapchain resize because idle was not verified: {}.",
				GetRHIGpuWaitResultName(idle));
			return;
		}
		m_SwapChain->Resize(width, height);
		m_BackBufferCompletionFences.assign(m_SwapChain->GetBufferCount(), {});
	}

	RHIGpuWaitResult DX12Context::WaitIdleFor(uint32_t timeoutMilliseconds) noexcept
	{
		if (!m_QueueSystem)
		{
			return RHIGpuWaitResult::Failed;
		}
		const RHIGpuWaitResult result = m_QueueSystem->WaitIdle(timeoutMilliseconds);
		if (result == RHIGpuWaitResult::Completed && m_UploadPool)
		{
			// Verified idle: no queue can still read pages quarantined after an unknown
			// completion. Retained allocators and descriptors complete through their points.
			m_UploadPool->ReleaseQuarantinedAfterVerifiedIdle();
		}
		return result;
	}

	bool DX12Context::IsPoisoned() const noexcept
	{
		return m_Poisoned || (m_Device && m_Device->HasCompletionError());
	}

	bool DX12Context::IsDeviceLost() const noexcept
	{
		return m_Device && (m_Device->HasObservedDeviceLoss() || m_Device->IsDeviceRemoved());
	}

	RHISubmissionVerification DX12Context::MakeSubmissionVerification() noexcept
	{
		return {
			.m_WaitForFence = [this](const RHIFencePoint& fence)
				{
					return m_QueueSystem->WaitForFenceCompletion(
						fence, RHIFrameVerificationTimeoutMilliseconds);
				},
			.m_WaitIdle = [this]()
				{
					return WaitIdleFor(RHIFrameVerificationTimeoutMilliseconds);
				},
		};
	}

	void DX12Context::RetireCompletedWork() noexcept
	{
		m_TransferManager->Reclaim();
		m_DescriptorManager->Tick();
		m_UploadPool->ReclaimCompleted();
		m_Device->RetireCompletedWork();
	}

	uint32_t DX12Context::GetFrameSlotCount() const noexcept
	{
		return static_cast<uint32_t>(m_Frames.size());
	}

	RHIDeviceIdentity DX12Context::GetDeviceIdentity() const noexcept
	{
		return m_Device ? m_Device->GetDeviceIdentity() : RHIDeviceIdentity{};
	}

	DX12ComputeCommandContext* DX12Context::AcquireComputeContext(DX12FrameContext& frame) noexcept
	{
		GGLAB_ASSERT_MSG(&frame == m_ActiveFrame && frame.m_Active,
			"Compute context can only be acquired from the active frame.");
		auto* context = &m_QueueSystem->GetComputeContext(frame.m_FrameSlotIndex);
		if (!frame.m_ComputeAllocator)
		{
			frame.m_ComputeAllocator =
				m_QueueSystem->GetAllocatorPool(DX12QueueType::Compute).RequestCommandAllocator();
			auto* commandList = &m_QueueSystem->GetComputeCommandList(frame.m_FrameSlotIndex);
			commandList->Begin(frame.m_ComputeAllocator);
			context->ClearTrackedResourceUses();
			context->BeginRecordingScope(frame.m_UploadWriter);
			BindGlobalDescriptorHeaps(*commandList);
		}
		return context;
	}

	DX12ComputeCommandContext& DX12Context::GetDirectComputeContext(
		DX12FrameContext& frame) noexcept
	{
		GGLAB_ASSERT_MSG(&frame == m_ActiveFrame && frame.m_Active,
			"A direct compute encoder can only be acquired from the active frame.");
		GGLAB_ASSERT(frame.m_FrameSlotIndex < m_DirectComputeContexts.size());
		return *m_DirectComputeContexts[frame.m_FrameSlotIndex];
	}

	void DX12Context::BeginGraphicsRecording(DX12FrameContext& frame) noexcept
	{
		frame.m_GraphicsAllocator =
			m_QueueSystem->GetAllocatorPool(DX12QueueType::Graphics).RequestCommandAllocator();
		auto* commandList = &m_QueueSystem->GetGraphicsCommandList(frame.m_FrameSlotIndex);
		commandList->Begin(frame.m_GraphicsAllocator);
		m_QueueSystem->GetGraphicsContext(frame.m_FrameSlotIndex).ClearTrackedResourceUses();
		m_DirectComputeContexts[frame.m_FrameSlotIndex]->ResetEncoderState();
		m_QueueSystem->GetGraphicsContext(frame.m_FrameSlotIndex).BeginRecordingScope(
			frame.m_UploadWriter);
		m_DirectComputeContexts[frame.m_FrameSlotIndex]->BeginRecordingScope(frame.m_UploadWriter);
		BindGlobalDescriptorHeaps(*commandList);
	}

	void DX12Context::BindGlobalDescriptorHeaps(DX12CommandList& commandList) noexcept
	{
		m_Device->BindGlobalDescriptorHeaps(commandList);
	}

	void DX12Context::FinishFrame(DX12FrameContext& frame, const RHIFencePoint& fencePoint) noexcept
	{
		m_QueueSystem->GetGraphicsContext(frame.m_FrameSlotIndex).EndRecordingScope();
		m_DirectComputeContexts[frame.m_FrameSlotIndex]->EndRecordingScope();
		if (frame.m_ComputeAllocator)
		{
			m_QueueSystem->GetComputeContext(frame.m_FrameSlotIndex).EndRecordingScope();
		}
		if (frame.m_Uploads)
		{
			frame.m_Uploads->Detach(frame.m_UploadWriter);
			frame.m_Uploads.reset();
		}
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
			GGLAB_UNUSED(AbortFrame(*m_ActiveFrame));
		}
		// A verified idle wait also released every quarantined upload page.
		const RHITeardownState teardown = QuiesceRHIForTeardown(*this);
		if (teardown == RHITeardownState::Unverified)
		{
			AbandonGpuOwners();
			return;
		}
		if (teardown == RHITeardownState::DeviceLost)
		{
			m_UploadPool->ReleaseQuarantinedAfterDeviceLoss();
		}
		m_BackBufferCompletionFences.clear();
		m_Frames.clear();
		m_UploadPool.reset();
		m_DirectComputeContexts.clear();
		m_TransferManager.reset();
		m_GpuProfiler.reset();
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

	void DX12Context::AbandonGpuOwners() noexcept
	{
		// Idle was not verified on a live device: destroying any owner could free memory the
		// GPU still reads. Process exit reclaims the abandoned owners.
		GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
			"DX12Context finalized without verified idle; GPU-visible owners are leaked.");
		for (auto& frame : m_Frames)
		{
			GGLAB_UNUSED(frame.release());
		}
		m_Frames.clear();
		for (auto& computeContext : m_DirectComputeContexts)
		{
			GGLAB_UNUSED(computeContext.release());
		}
		m_DirectComputeContexts.clear();
		GGLAB_UNUSED(m_UploadPool.release());
		GGLAB_UNUSED(m_TransferManager.release());
		GGLAB_UNUSED(m_GpuProfiler.release());
		GGLAB_UNUSED(m_SwapChain.release());
		GGLAB_UNUSED(m_DescriptorManager.release());
		GGLAB_UNUSED(m_PipelineSystem.release());
		GGLAB_UNUSED(m_QueueSystem.release());
		GGLAB_UNUSED(m_Device.release());
		m_BackBufferCompletionFences.clear();
		m_Initialized = false;
	}
}
