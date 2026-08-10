#include "Core/Precompiled.h"
#include "Graphics/RHI/Vulkan/VulkanFrameRuntime.h"
#include "Graphics/RHI/Vulkan/VulkanBarrier.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <format>

namespace gglab
{
	namespace
	{
		// Shared by both command buffers: stage/access selection keeps the
		// timeline-edge policy simple (ALL_COMMANDS in -> COLOR_ATTACHMENT for
		// writes). The final transition to PRESENT_SRC_KHR needs no write
		// access; the presenting semaphore orders the handoff.
		constexpr VkPipelineStageFlags2 kAllStages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		constexpr VkAccessFlags2 kAllAccess =
			VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		constexpr VkPipelineStageFlags2 kColorWriteStage =
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		constexpr VkAccessFlags2 kColorWriteAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

		VkImageMemoryBarrier2 MakePresentImageBarrier(VkImage image,
			VkImageLayout oldLayout, VkImageLayout newLayout, bool hasWriteSource,
			bool hasWriteDestination) noexcept
		{
			return MakeVulkanImageBarrier(image, VK_IMAGE_ASPECT_COLOR_BIT,
				oldLayout, newLayout,
				hasWriteSource ? kAllStages : VK_PIPELINE_STAGE_2_NONE,
				hasWriteSource ? kAllAccess : VK_ACCESS_2_NONE,
				hasWriteDestination ? kColorWriteStage : VK_PIPELINE_STAGE_2_NONE,
				hasWriteDestination ? kColorWriteAccess : VK_ACCESS_2_NONE);
		}
	}

	VulkanAcquireOutcome ClassifyVulkanAcquireResult(const VkResult result) noexcept
	{
		switch (result)
		{
		case VK_SUCCESS:
			return VulkanAcquireOutcome::Acquired;
		case VK_SUBOPTIMAL_KHR:
			return VulkanAcquireOutcome::RecreatePending;
		case VK_ERROR_OUT_OF_DATE_KHR:
			// No image was handed over; the caller recreates the swapchain
			// with the real drawable extent and retries. The runtime never
			// recreates from its own cached extent.
			return VulkanAcquireOutcome::OutOfDate;
		default:
			return VulkanAcquireOutcome::Fatal;
		}
	}

	VulkanPresentOutcome ClassifyVulkanPresentResult(const VkResult result) noexcept
	{
		switch (result)
		{
		case VK_SUCCESS:
			return VulkanPresentOutcome::Presented;
		case VK_SUBOPTIMAL_KHR:
		case VK_ERROR_OUT_OF_DATE_KHR:
			// The submission already completed; the frame is done. Surface
			// recreation is scheduled at the next safe point.
			return VulkanPresentOutcome::RecreatePending;
		default:
			return VulkanPresentOutcome::Failed;
		}
	}

	VulkanFrameTransactionOutcome ClassifySubmitPresentTransaction(
		const VkResult submitResult, const VkResult presentResult) noexcept
	{
		if (submitResult != VK_SUCCESS)
		{
			return VulkanFrameTransactionOutcome::SubmitFailed;
		}
		switch (presentResult)
		{
		case VK_SUCCESS:
			return VulkanFrameTransactionOutcome::Completed;
		case VK_SUBOPTIMAL_KHR:
		case VK_ERROR_OUT_OF_DATE_KHR:
			// The graphics submission and its timeline signal are still
			// valid; only the swapchain needs recreation.
			return VulkanFrameTransactionOutcome::RecreatePending;
		default:
			return VulkanFrameTransactionOutcome::PresentFailed;
		}
	}

	uint64_t UpdateSlotReuseGate(const uint64_t previousGate,
		const VkResult submitResult, const uint64_t candidateValue) noexcept
	{
		// Only a successfully submitted timeline value may become the reuse
		// gate: a failed submission must never leave a future frame waiting
		// on a value that was never signaled.
		return submitResult == VK_SUCCESS ? candidateValue : previousGate;
	}

	bool IsVulkanDeviceLostError(const VkResult result) noexcept
	{
		return result == VK_ERROR_DEVICE_LOST;
	}

	VulkanRuntimeHealthState UpdateVulkanRuntimeHealth(
		const VulkanRuntimeHealthState& state, const VkResult error) noexcept
	{
		VulkanRuntimeHealthState next = state;
		if (error != VK_SUCCESS)
		{
			next.m_Fatal = true;
			next.m_DeviceLost = next.m_DeviceLost || IsVulkanDeviceLostError(error);
		}
		return next;
	}

	VulkanBeginGateOutcome ClassifyVulkanBeginGateResult(const VkResult waitResult) noexcept
	{
		return waitResult == VK_SUCCESS ? VulkanBeginGateOutcome::Ready
			: VulkanBeginGateOutcome::Fatal;
	}

	VulkanFrameIndexModel::VulkanFrameIndexModel(uint32_t frameSlotCount) noexcept
		: m_FrameSlotCount(std::max(frameSlotCount, 1u))
	{
	}

	uint32_t VulkanFrameIndexModel::NextFrameSlot() noexcept
	{
		const uint32_t slot = m_NextSlotIndex;
		m_NextSlotIndex = (m_NextSlotIndex + 1) % m_FrameSlotCount;
		return slot;
	}

	void VulkanFrameIndexModel::CommitFrame(uint32_t frameSlot, uint32_t backBufferIndex) noexcept
	{
		m_FramePairs.emplace_back(frameSlot, backBufferIndex);
	}

	void VulkanFrameIndexModel::ResetFramePairs() noexcept
	{
		m_FramePairs.clear();
		m_NextSlotIndex = 0;
	}

	void VulkanImageLayoutTracker::Reset(uint32_t imageCount) noexcept
	{
		m_Layouts.assign(imageCount, VulkanPresentImageLayout::Undefined);
	}

	VulkanPresentImageLayout VulkanImageLayoutTracker::Get(uint32_t image) const noexcept
	{
		return image < m_Layouts.size()
			? m_Layouts[image]
			: VulkanPresentImageLayout::Undefined;
	}

	void VulkanImageLayoutTracker::Set(uint32_t image, VulkanPresentImageLayout layout) noexcept
	{
		if (image < m_Layouts.size())
		{
			m_Layouts[image] = layout;
		}
	}

	void VulkanFrameSlotStateMachine::Reset(uint32_t slotCount) noexcept
	{
		m_Phases.assign(slotCount, VulkanFrameSlotPhase::Idle);
	}

	bool VulkanFrameSlotStateMachine::TryBegin(uint32_t slot) noexcept
	{
		// A slot begins a fresh transaction from Idle or after a completed
		// End/Abort of the previous transaction; only a Begin while the
		// transaction is still active is rejected.
		if (slot >= m_Phases.size() || m_Phases[slot] == VulkanFrameSlotPhase::Begun)
		{
			return false;
		}
		m_Phases[slot] = VulkanFrameSlotPhase::Begun;
		return true;
	}

	bool VulkanFrameSlotStateMachine::TryEnd(uint32_t slot) noexcept
	{
		if (slot >= m_Phases.size() || m_Phases[slot] != VulkanFrameSlotPhase::Begun)
		{
			return false;
		}
		m_Phases[slot] = VulkanFrameSlotPhase::Ended;
		return true;
	}

	bool VulkanFrameSlotStateMachine::TryAbort(uint32_t slot) noexcept
	{
		if (slot >= m_Phases.size() || m_Phases[slot] != VulkanFrameSlotPhase::Begun)
		{
			return false;
		}
		m_Phases[slot] = VulkanFrameSlotPhase::Aborted;
		return true;
	}

	bool VulkanFrameSlotStateMachine::IsActive(uint32_t slot) const noexcept
	{
		return slot < m_Phases.size() && m_Phases[slot] == VulkanFrameSlotPhase::Begun;
	}

	VulkanFrameSlotPhase VulkanFrameSlotStateMachine::GetPhase(uint32_t slot) const noexcept
	{
		return slot < m_Phases.size() ? m_Phases[slot] : VulkanFrameSlotPhase::Idle;
	}

	VulkanFrameRuntime::~VulkanFrameRuntime()
	{
		// Quiesce and drain frame-scoped descriptor state while the graphics
		// timeline is still available as the device completion source.
		Finalize();

		// The device outlives the runtime, so unregister the borrowed timeline
		// before its owner is destroyed.
		if (m_Device != nullptr && m_Timeline != nullptr &&
			m_Device->GetGraphicsTimeline() == m_Timeline.get())
		{
			m_Device->SetGraphicsTimeline(nullptr);
		}
	}

	void VulkanFrameRuntime::Finalize() noexcept
	{
		if (m_Finalized)
		{
			return;
		}
		m_Finalized = true;
		// Only a lost VkDevice skips the quiesce: its waits cannot complete
		// normally. An ordinary fatal error (surface lost, out of memory,
		// initialization failure) still has valid in-flight work that must
		// complete before the command pools and semaphores are destroyed.
		if (m_Device != nullptr && !m_DeviceLost)
		{
			// Best-effort quiesce in the destructor: the wait results cannot
			// be surfaced here, but the call itself is not skipped.
			vkQueueWaitIdle(m_Device->GetGraphicsQueue());
			if (m_Timeline != nullptr)
			{
				(void)m_Timeline->Wait(m_Timeline->GetCurrentSignalValue());
			}
		}
		if (m_Device != nullptr && m_DescriptorFrameTrackingAttached)
		{
			if (m_ActiveFrame.has_value())
			{
				(void)m_Device->GetDescriptorManager().AbortFrameSnapshot(
					m_ActiveFrame->m_FrameSlotIndex);
				m_ActiveFrame.reset();
			}
			m_Device->RetireCompletedWork();
			if (m_Device->GetDescriptorManager().DetachFrameTracking())
			{
				m_DescriptorFrameTrackingAttached = false;
			}
		}
		DestroyFrameSlots();
	}

	VulkanFrameRuntime::Result VulkanFrameRuntime::Create(
		const VulkanFrameRuntimeCreateInfo& createInfo) noexcept
	{
		Result result{};
		if (createInfo.m_Device == nullptr || createInfo.m_Surface == nullptr
			|| createInfo.m_Instance == nullptr || createInfo.m_Snapshot == nullptr
			|| createInfo.m_Width == 0 || createInfo.m_Height == 0)
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			result.m_Error = "VulkanFrameRuntime requires instance, surface, device, snapshot "
				"and a nonzero drawable extent.";
			return result;
		}

		auto runtime = std::make_unique<VulkanFrameRuntime>();
		runtime->m_Device = createInfo.m_Device;
		runtime->m_Vsync = createInfo.m_Vsync;

		// Graphics timeline gate.
		VulkanTimelineFence::CreateInfo timelineInfo{};
		timelineInfo.m_Device = createInfo.m_Device->Get();
		VulkanTimelineFence::Result timelineResult = VulkanTimelineFence::Create(timelineInfo);
		if (!timelineResult.Succeeded())
		{
			result.m_Result = timelineResult.m_Result;
			result.m_Error = std::format("Timeline fence creation failed: {}", timelineResult.m_Error);
			return result;
		}
		runtime->m_Timeline = std::move(timelineResult.m_Fence);

		// Register the timeline as the device's graphics completion source
		// so resource retirement can resolve RHIFencePoints against it.
		createInfo.m_Device->SetGraphicsTimeline(runtime->m_Timeline.get());

		// Frame slots: binary imageAvailable semaphore, command pool and the
		// two-purpose command buffers.
		runtime->m_StateMachine.Reset(createInfo.m_FrameSlotCount);
		runtime->m_FrameSlots.resize(createInfo.m_FrameSlotCount);
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = createInfo.m_Snapshot->m_GraphicsPresentQueueFamilyIndex;
		VkCommandBufferAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 2;
		for (uint32_t i = 0; i < createInfo.m_FrameSlotCount; ++i)
		{
			VulkanFrameSlot& slot = runtime->m_FrameSlots[i];
			slot.m_Index = i;
			if (vkCreateSemaphore(createInfo.m_Device->Get(), &semaphoreInfo, nullptr,
				&slot.m_ImageAvailable)
				!= VK_SUCCESS)
			{
				result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
				result.m_Error = std::format("vkCreateSemaphore for frame slot {} failed.", i);
				return result;
			}
			if (vkCreateCommandPool(createInfo.m_Device->Get(), &poolInfo, nullptr,
				&slot.m_CommandPool)
				!= VK_SUCCESS)
			{
				result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
				result.m_Error = std::format("vkCreateCommandPool for frame slot {} failed.", i);
				return result;
			}
			allocateInfo.commandPool = slot.m_CommandPool;
			VkCommandBuffer commandBuffers[2] = {};
			if (vkAllocateCommandBuffers(createInfo.m_Device->Get(), &allocateInfo,
				commandBuffers)
				!= VK_SUCCESS)
			{
				result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
				result.m_Error = std::format("vkAllocateCommandBuffers for frame slot {} failed.", i);
				return result;
			}
			slot.m_NormalCommandBuffer = commandBuffers[0];
			slot.m_AbortCommandBuffer = commandBuffers[1];
		}

		// Swapchain.
		VulkanSwapChainCreateInfo swapChainInfo{};
		swapChainInfo.m_PhysicalDevice = createInfo.m_PhysicalDevice;
		swapChainInfo.m_Device = createInfo.m_Device->Get();
		swapChainInfo.m_Surface = createInfo.m_Surface->Get();
		swapChainInfo.m_RequestedFormat = createInfo.m_RequestedFormat;
		swapChainInfo.m_Vsync = createInfo.m_Vsync;
		swapChainInfo.m_Width = createInfo.m_Width;
		swapChainInfo.m_Height = createInfo.m_Height;
		VulkanSwapChain::Result swapChainResult = VulkanSwapChain::Create(swapChainInfo);
		if (!swapChainResult.Succeeded())
		{
			result.m_Result = swapChainResult.m_Result;
			result.m_Error =
				std::format("Swapchain creation failed: {}", swapChainResult.m_Error);
			return result;
		}
		runtime->m_SwapChain = std::move(swapChainResult.m_SwapChain);

		// Model state sized to the runtime.
		runtime->m_IndexModel = VulkanFrameIndexModel(createInfo.m_FrameSlotCount);
		runtime->m_LayoutTracker.Reset(runtime->m_SwapChain->GetImageCount());
		if (!createInfo.m_Device->GetDescriptorManager().InitializeFrameTracking(
			createInfo.m_FrameSlotCount))
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			result.m_Error = "Descriptor frame tracking initialization failed.";
			return result;
		}
		runtime->m_DescriptorFrameTrackingAttached = true;

		result.m_Runtime = std::move(runtime);
		return result;
	}

	VulkanBeginFrameResult VulkanFrameRuntime::BeginFrame() noexcept
	{
		VulkanBeginFrameResult result{};
		if (m_Fatal || m_SwapChain == nullptr)
		{
			// Fatal does not imply a lost device; report the state that
			// matches the actual health.
			result.m_Result = m_DeviceLost ? VK_ERROR_DEVICE_LOST : VK_ERROR_UNKNOWN;
			return result;
		}

		// A new frame must never be started while a previous transaction is
		// still active: an acquire with an unconsumed imageAvailable
		// semaphore would leak the acquire transaction. This is checked
		// before vkAcquireNextImageKHR is called.
		if (m_ActiveFrame.has_value())
		{
			result.m_Result = VK_ERROR_UNKNOWN;
			return result;
		}

		// The caller must not call BeginFrame at a zero extent; enforce as a
		// cheap guard so the acquire never hangs on an impossible image.
		const VkExtent2D extent = m_SwapChain->GetExtent();
		if (extent.width == 0 || extent.height == 0)
		{
			result.m_Result = VK_ERROR_OUT_OF_DATE_KHR;
			return result;
		}

		const uint32_t frameSlotIndex = m_IndexModel.NextFrameSlot();
		VulkanFrameSlot& slot = m_FrameSlots[frameSlotIndex];

		// Reuse gate: the slot may not be reused until its last successfully
		// submitted timeline value has completed. No frame may ever be
		// recorded into a command buffer that is still being executed. A
		// failed wait stops BeginFrame before the command-pool reset and the
		// acquire.
		if (slot.m_LastSubmittedTimelineValue != 0)
		{
			const VkResult waitResult = m_Timeline->Wait(slot.m_LastSubmittedTimelineValue);
			if (ClassifyVulkanBeginGateResult(waitResult) != VulkanBeginGateOutcome::Ready)
			{
				MarkFatal(waitResult);
				result.m_Status = VulkanAcquireOutcome::Fatal;
				result.m_Result = waitResult;
				return result;
			}
		}
		vkResetCommandPool(m_Device->Get(), slot.m_CommandPool, 0);
		if (!m_Device->GetDescriptorManager().BeginFrameSnapshot(frameSlotIndex))
		{
			MarkFatal(VK_ERROR_INITIALIZATION_FAILED);
			result.m_Status = VulkanAcquireOutcome::Fatal;
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			return result;
		}

		uint32_t backBufferIndex = 0;
		const VkResult acquireResult = vkAcquireNextImageKHR(m_Device->Get(), m_SwapChain->Get(),
			UINT64_MAX, slot.m_ImageAvailable, VK_NULL_HANDLE, &backBufferIndex);

		const VulkanAcquireOutcome outcome = ClassifyVulkanAcquireResult(acquireResult);
		switch (outcome)
		{
		case VulkanAcquireOutcome::Acquired:
		case VulkanAcquireOutcome::RecreatePending:
			if (!m_StateMachine.TryBegin(frameSlotIndex))
			{
				// Defensive: the active-frame check above already prevents a
				// double Begin, so a rejected slot indicates a corrupted
				// transaction state.
				MarkFatal(VK_ERROR_INITIALIZATION_FAILED);
				(void)m_Device->GetDescriptorManager().AbortFrameSnapshot(frameSlotIndex);
				result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
				return result;
			}
			m_IndexModel.CommitFrame(frameSlotIndex, backBufferIndex);
			m_ActiveFrame = VulkanActiveFrame{ frameSlotIndex, backBufferIndex };
			result.m_Status = outcome;
			result.m_FrameSlotIndex = frameSlotIndex;
			result.m_BackBufferIndex = backBufferIndex;
			result.m_RecreatePending = outcome == VulkanAcquireOutcome::RecreatePending;
			result.m_Result = acquireResult;
			return result;
		case VulkanAcquireOutcome::OutOfDate:
			// No image was handed over and no frame transaction started.
			// The caller owns the drawable extent and must recreate the
			// swapchain with the real extent before retrying; the runtime
			// never picks an extent itself.
			result.m_Status = VulkanAcquireOutcome::OutOfDate;
			(void)m_Device->GetDescriptorManager().AbortFrameSnapshot(frameSlotIndex);
			result.m_Result = acquireResult;
			return result;
		default:
			(void)m_Device->GetDescriptorManager().AbortFrameSnapshot(frameSlotIndex);
			MarkFatal(acquireResult);
			result.m_Status = VulkanAcquireOutcome::Fatal;
			result.m_Result = acquireResult;
			return result;
		}
	}

	VulkanSubmitPresentResult VulkanFrameRuntime::EndFrame(
		const std::array<float, 4>& clearColor) noexcept
	{
		const VulkanFrameRecording recording = BeginFrameRecording();
		if (!recording.IsValid())
		{
			return {};
		}

		VkRenderingAttachmentInfo colorAttachment{};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colorAttachment.imageView = recording.m_BackBufferView;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		std::ranges::copy(clearColor, colorAttachment.clearValue.color.float32);

		VkRenderingInfo renderingInfo{};
		renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderingInfo.renderArea.extent = recording.m_Extent;
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;
		vkCmdBeginRendering(recording.m_CommandBuffer, &renderingInfo);
		vkCmdEndRendering(recording.m_CommandBuffer);
		if (!EndFrameRecording())
		{
			return {};
		}
		return EndFrame();
	}

	VulkanFrameRecording VulkanFrameRuntime::BeginFrameRecording() noexcept
	{
		if (!m_ActiveFrame.has_value() || m_NormalRecordingOpen || m_NormalRecordingReady)
		{
			return {};
		}
		const VulkanActiveFrame active = *m_ActiveFrame;
		VulkanFrameSlot& slot = m_FrameSlots[active.m_FrameSlotIndex];
		const VkImage swapchainImage = m_SwapChain->GetImage(active.m_BackBufferIndex).m_Image;
		const VkImageView swapchainView = m_SwapChain->GetImageView(active.m_BackBufferIndex);
		const VkExtent2D extent = m_SwapChain->GetExtent();

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		const VkResult beginResult = vkBeginCommandBuffer(slot.m_NormalCommandBuffer, &beginInfo);
		if (beginResult != VK_SUCCESS)
		{
			MarkFatal(beginResult);
			return {};
		}

		const VulkanPresentImageLayout tracked = m_LayoutTracker.Get(active.m_BackBufferIndex);
		const VkImageLayout oldLayout = tracked == VulkanPresentImageLayout::Present
			? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
			: VK_IMAGE_LAYOUT_UNDEFINED;
		const VkImageMemoryBarrier2 barrier = MakePresentImageBarrier(swapchainImage,
			oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true, true);
		VkDependencyInfo dependencyInfo{};
		dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(slot.m_NormalCommandBuffer, &dependencyInfo);

		m_NormalRecordingOpen = true;
		return {
			.m_CommandBuffer = slot.m_NormalCommandBuffer,
			.m_BackBufferImage = swapchainImage,
			.m_BackBufferView = swapchainView,
			.m_Extent = extent,
			.m_FrameSlotIndex = active.m_FrameSlotIndex,
			.m_BackBufferIndex = active.m_BackBufferIndex,
		};
	}

	bool VulkanFrameRuntime::EndFrameRecording() noexcept
	{
		if (!m_ActiveFrame.has_value() || !m_NormalRecordingOpen || m_NormalRecordingReady)
		{
			return false;
		}
		const VulkanActiveFrame active = *m_ActiveFrame;
		VulkanFrameSlot& slot = m_FrameSlots[active.m_FrameSlotIndex];
		const VkImage swapchainImage = m_SwapChain->GetImage(active.m_BackBufferIndex).m_Image;
		const VkImageMemoryBarrier2 barrier = MakePresentImageBarrier(swapchainImage,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, true, false);
		VkDependencyInfo dependencyInfo{};
		dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(slot.m_NormalCommandBuffer, &dependencyInfo);
		const VkResult endResult = vkEndCommandBuffer(slot.m_NormalCommandBuffer);
		m_NormalRecordingOpen = false;
		if (endResult != VK_SUCCESS)
		{
			MarkFatal(endResult);
			return false;
		}
		m_NormalRecordingReady = true;
		return true;
	}

	VulkanSubmitPresentResult VulkanFrameRuntime::EndFrame() noexcept
	{
		VulkanSubmitPresentResult result{};
		if (!m_ActiveFrame.has_value() || !m_NormalRecordingReady)
		{
			// No active frame: the transaction is already complete or was
			// never started.
			return result;
		}
		const VulkanActiveFrame active = *m_ActiveFrame;
		VulkanFrameSlot& slot = m_FrameSlots[active.m_FrameSlotIndex];
		if (!m_StateMachine.TryEnd(active.m_FrameSlotIndex))
		{
			// End after Abort is rejected; the abort already released the
			// image.
			(void)m_Device->GetDescriptorManager().AbortFrameSnapshot(
				active.m_FrameSlotIndex);
			m_ActiveFrame.reset();
			m_NormalRecordingOpen = false;
			m_NormalRecordingReady = false;
			return result;
		}
		VulkanSubmitPresentResult submitPresent =
			SubmitAndPresent(active.m_FrameSlotIndex, active.m_BackBufferIndex,
				slot.m_NormalCommandBuffer);
		const bool snapshotAccepted = submitPresent.m_Submitted
			? m_Device->GetDescriptorManager().SubmitFrameSnapshot(
				active.m_FrameSlotIndex, submitPresent.m_SubmittedFencePoint)
			: m_Device->GetDescriptorManager().AbortFrameSnapshot(active.m_FrameSlotIndex);
		if (!snapshotAccepted)
		{
			MarkFatal(VK_ERROR_INITIALIZATION_FAILED);
			submitPresent.m_Fatal = true;
		}
		m_ActiveFrame.reset();
		m_NormalRecordingReady = false;
		if (submitPresent.m_Presented)
		{
			m_LayoutTracker.Set(active.m_BackBufferIndex, VulkanPresentImageLayout::Present);
		}
		return submitPresent;
	}

	VulkanSubmitPresentResult VulkanFrameRuntime::AbortFrame() noexcept
	{
		VulkanSubmitPresentResult result{};
		if (!m_ActiveFrame.has_value())
		{
			// No active frame: the transaction is already complete or was
			// never started.
			return result;
		}
		const VulkanActiveFrame active = *m_ActiveFrame;
		m_NormalRecordingOpen = false;
		m_NormalRecordingReady = false;
		VulkanFrameSlot& slot = m_FrameSlots[active.m_FrameSlotIndex];
		if (!m_StateMachine.TryAbort(active.m_FrameSlotIndex))
		{
			// Abort after End is rejected; the frame was already presented.
			(void)m_Device->GetDescriptorManager().AbortFrameSnapshot(
				active.m_FrameSlotIndex);
			m_ActiveFrame.reset();
			return result;
		}
		// The partially recorded normal command buffer is never submitted;
		// the dedicated abort command buffer releases the acquired image.
		RecordAbortFrame(slot.m_AbortCommandBuffer, active.m_BackBufferIndex);
		VulkanSubmitPresentResult submitPresent =
			SubmitAndPresent(active.m_FrameSlotIndex, active.m_BackBufferIndex,
				slot.m_AbortCommandBuffer);
		if (!m_Device->GetDescriptorManager().AbortFrameSnapshot(active.m_FrameSlotIndex))
		{
			MarkFatal(VK_ERROR_INITIALIZATION_FAILED);
			submitPresent.m_Fatal = true;
		}
		m_ActiveFrame.reset();
		if (submitPresent.m_Presented)
		{
			m_LayoutTracker.Set(active.m_BackBufferIndex, VulkanPresentImageLayout::Present);
		}
		return submitPresent;
	}

	bool VulkanFrameRuntime::RecreateSwapChain(uint32_t width, uint32_t height,
		bool vsync, std::string& outError) noexcept
	{
		if (m_Fatal || m_SwapChain == nullptr)
		{
			outError = m_Fatal
				? "Cannot recreate the swapchain after a fatal runtime error."
				: "No swapchain to recreate.";
			return false;
		}
		if (width == 0 || height == 0)
		{
			outError = "Recreate requires a nonzero extent.";
			return false;
		}
		// Swapchain recreation is a safe point: no frame may be active and no
		// queued work may reference old swapchain images.
		if (m_ActiveFrame.has_value())
		{
			outError = "Cannot recreate the swapchain while a frame is active.";
			return false;
		}
		const VkResult idleResult = WaitIdle();
		if (idleResult != VK_SUCCESS)
		{
			MarkFatal(idleResult);
			outError = std::format("WaitIdle before swapchain recreate failed with {}.",
				ToString(idleResult));
			return false;
		}
		const bool recreated = m_SwapChain->Recreate(width, height, vsync, outError);
		if (recreated)
		{
			m_Vsync = vsync;
			m_LayoutTracker.Reset(m_SwapChain->GetImageCount());
			m_IndexModel.ResetFramePairs();
		}
		else
		{
			MarkFatal(VK_ERROR_INITIALIZATION_FAILED);
		}
		return recreated;
	}

	VkResult VulkanFrameRuntime::WaitIdle() noexcept
	{
		if (m_Device == nullptr || m_Timeline == nullptr)
		{
			return VK_ERROR_INITIALIZATION_FAILED;
		}
		const VkResult queueResult = vkQueueWaitIdle(m_Device->GetGraphicsQueue());
		if (queueResult != VK_SUCCESS)
		{
			return queueResult;
		}
		return m_Timeline->Wait(m_Timeline->GetCurrentSignalValue());
	}

	void VulkanFrameRuntime::RecordAbortFrame(
		VkCommandBuffer commandBuffer, uint32_t backBufferIndex) noexcept
	{
		const VkImage swapchainImage = m_SwapChain->GetImage(backBufferIndex).m_Image;

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		// A skipped frame leaves the image in whatever layout the previous
		// frame chose; only a layout other than PRESENT_SRC_KHR needs a
		// transition. Undefined is a full discard.
		const VulkanPresentImageLayout tracked = m_LayoutTracker.Get(backBufferIndex);
		if (tracked != VulkanPresentImageLayout::Present)
		{
			const VkImageLayout oldLayout = tracked == VulkanPresentImageLayout::ColorAttachment
				? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
				: VK_IMAGE_LAYOUT_UNDEFINED;
			const VkImageMemoryBarrier2 barrier = MakePresentImageBarrier(swapchainImage,
				oldLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, tracked == VulkanPresentImageLayout::ColorAttachment,
				false);
			VkDependencyInfo dependencyInfo{};
			dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			dependencyInfo.imageMemoryBarrierCount = 1;
			dependencyInfo.pImageMemoryBarriers = &barrier;
			vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
		}

		vkEndCommandBuffer(commandBuffer);
	}

	VulkanSubmitPresentResult VulkanFrameRuntime::SubmitAndPresent(uint32_t frameSlotIndex,
		uint32_t backBufferIndex, VkCommandBuffer commandBuffer) noexcept
	{
		VulkanSubmitPresentResult result{};
		VulkanFrameSlot& slot = m_FrameSlots[frameSlotIndex];

		// Reserve a candidate timeline value; it only becomes a valid wait
		// target (and the slot reuse gate) when the submission succeeds.
		const uint64_t timelineValue = m_Timeline->ReserveSignalValue();

		VkCommandBufferSubmitInfo commandBufferInfo{};
		commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		commandBufferInfo.commandBuffer = commandBuffer;

		VkSemaphoreSubmitInfo waitInfo{};
		waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		waitInfo.semaphore = slot.m_ImageAvailable;
		waitInfo.stageMask = kAllStages;

		VkSemaphoreSubmitInfo timelineSignal{};
		timelineSignal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		timelineSignal.semaphore = m_Timeline->Get();
		timelineSignal.value = timelineValue;
		timelineSignal.stageMask = kAllStages;

		VkSemaphoreSubmitInfo presentSignal = timelineSignal;
		presentSignal.semaphore = m_SwapChain->GetRenderingFinished(backBufferIndex);
		std::array<VkSemaphoreSubmitInfo, 2> signalInfos = { timelineSignal, presentSignal };

		VkSubmitInfo2 submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submitInfo.waitSemaphoreInfoCount = 1;
		submitInfo.pWaitSemaphoreInfos = &waitInfo;
		submitInfo.commandBufferInfoCount = 1;
		submitInfo.pCommandBufferInfos = &commandBufferInfo;
		submitInfo.signalSemaphoreInfoCount = 2;
		submitInfo.pSignalSemaphoreInfos = signalInfos.data();

		const VkResult submitResult =
			vkQueueSubmit2(m_Device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
		result.m_Result = submitResult;
		if (submitResult != VK_SUCCESS)
		{
			// The reserved value was never signaled; the reuse gate must not
			// move to it. The runtime enters the fatal state so no further
			// frame can wait on it.
			MarkFatal(submitResult);
			result.m_Fatal = true;
			return result;
		}
		result.m_Submitted = true;
		m_Timeline->CommitSubmittedValue(timelineValue);
		slot.m_LastSubmittedTimelineValue =
			UpdateSlotReuseGate(slot.m_LastSubmittedTimelineValue, submitResult, timelineValue);
		result.m_SubmittedFencePoint =
			RHIFencePoint(m_Timeline->GetRHIHandle(), timelineValue);

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		const VkSemaphore renderingFinished =
			m_SwapChain->GetRenderingFinished(backBufferIndex);
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderingFinished;
		const VkSwapchainKHR swapChainHandle = m_SwapChain->Get();
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapChainHandle;
		presentInfo.pImageIndices = &backBufferIndex;
		const VkResult presentResult =
			vkQueuePresentKHR(m_Device->GetGraphicsQueue(), &presentInfo);
		result.m_Result = presentResult;

		switch (ClassifySubmitPresentTransaction(submitResult, presentResult))
		{
		case VulkanFrameTransactionOutcome::Completed:
			result.m_Presented = true;
			break;
		case VulkanFrameTransactionOutcome::RecreatePending:
			// The submission and its timeline signal stand; only the
			// swapchain needs recreation at the next safe point.
			result.m_Presented = true;
			result.m_RecreatePending = true;
			break;
		case VulkanFrameTransactionOutcome::PresentFailed:
			// The submission stays valid and its timeline signal was
			// committed; the failure only stops the runtime. Cleanup still
			// quiesces unless the error is a lost device.
			MarkFatal(presentResult);
			result.m_Fatal = true;
			break;
		case VulkanFrameTransactionOutcome::SubmitFailed:
			// Unreachable: handled before present.
			MarkFatal(submitResult);
			result.m_Fatal = true;
			break;
		}
		return result;
	}

	VkSemaphore VulkanFrameRuntime::GetImageAvailableSemaphore(uint32_t frameSlotIndex) const noexcept
	{
		return frameSlotIndex < m_FrameSlots.size()
			? m_FrameSlots[frameSlotIndex].m_ImageAvailable
			: VK_NULL_HANDLE;
	}

	VkSemaphore VulkanFrameRuntime::GetRenderingFinishedSemaphore(uint32_t backBufferIndex) const noexcept
	{
		return m_SwapChain ? m_SwapChain->GetRenderingFinished(backBufferIndex) : VK_NULL_HANDLE;
	}

	bool VulkanFrameRuntime::IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept
	{
		if (!fencePoint.IsValid())
		{
			return true;
		}
		if (m_Timeline == nullptr || fencePoint.m_Fence != m_Timeline->GetRHIHandle())
		{
			// An unknown fence is not a completed fence; resource
			// retirement must never assume a foreign point finished.
			return false;
		}
		uint64_t completedValue = 0;
		if (m_Timeline->GetCompletedValue(completedValue) != VK_SUCCESS)
		{
			return false;
		}
		return completedValue >= fencePoint.m_Value;
	}

	void VulkanFrameRuntime::MarkFatal(const VkResult error) noexcept
	{
		const VulkanRuntimeHealthState next =
			UpdateVulkanRuntimeHealth(VulkanRuntimeHealthState{ m_Fatal, m_DeviceLost }, error);
		m_Fatal = next.m_Fatal;
		m_DeviceLost = next.m_DeviceLost;
	}

	void VulkanFrameRuntime::DestroyFrameSlots() noexcept
	{
		for (VulkanFrameSlot& slot : m_FrameSlots)
		{
			if (slot.m_CommandPool != VK_NULL_HANDLE)
			{
				vkDestroyCommandPool(m_Device->Get(), slot.m_CommandPool, nullptr);
				slot.m_CommandPool = VK_NULL_HANDLE;
			}
			if (slot.m_ImageAvailable != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(m_Device->Get(), slot.m_ImageAvailable, nullptr);
				slot.m_ImageAvailable = VK_NULL_HANDLE;
			}
		}
		m_FrameSlots.clear();
	}
}
