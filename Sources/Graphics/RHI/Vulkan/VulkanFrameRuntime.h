#pragma once
#include "Graphics/RHI/RHITypes.h"
#include "Graphics/RHI/Vulkan/VulkanAdapter.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanInstance.h"
#include "Graphics/RHI/Vulkan/VulkanSwapChain.h"
#include "Graphics/RHI/Vulkan/VulkanTimelineFence.h"
#include "Graphics/RHI/Vulkan/VulkanWin32Surface.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gglab
{
	// CPU models. Pure state logic shared by the runtime and the synthetic
	// contract tests; the runtime never keeps a second state machine.

	enum class VulkanFrameSlotPhase : uint8_t
	{
		Idle,
		Begun,
		Ended,
		Aborted,
	};

	// Classifies vkAcquireNextImageKHR results. Only SUCCESS/SUBOPTIMAL hand
	// over an image; OUT_OF_DATE hands over nothing and the caller must
	// recreate the swapchain with the real drawable extent and retry.
	enum class VulkanAcquireOutcome : uint8_t
	{
		Acquired,
		RecreatePending,   // SUBOPTIMAL: image valid, schedule recreate
		OutOfDate,         // OUT_OF_DATE: no image; caller recreates and retries
		Fatal,
	};

	// Classifies the frame transaction produced by a successful submission
	// followed by vkQueuePresentKHR. OUT_OF_DATE is not fatal: the graphics
	// submission already completed and the frame-slot timeline stays valid.
	enum class VulkanPresentOutcome : uint8_t
	{
		Presented,
		RecreatePending,   // SUBOPTIMAL or OUT_OF_DATE: recreate at safe point
		Failed,
	};

	[[nodiscard]] VulkanAcquireOutcome ClassifyVulkanAcquireResult(VkResult result) noexcept;
	[[nodiscard]] VulkanPresentOutcome ClassifyVulkanPresentResult(VkResult result) noexcept;

	// Combined submit+present transaction result. A failed submit never
	// reaches present and never updates the frame-slot reuse gate; a
	// non-fatal present result keeps the submission valid.
	enum class VulkanFrameTransactionOutcome : uint8_t
	{
		Completed,        // submit and present succeeded
		RecreatePending,  // submit succeeded, present SUBOPTIMAL/OUT_OF_DATE
		SubmitFailed,     // fatal: queue submit rejected
		PresentFailed,    // fatal: queue present rejected
	};

	[[nodiscard]] VulkanFrameTransactionOutcome ClassifySubmitPresentTransaction(
		VkResult submitResult, VkResult presentResult) noexcept;

	// Frame-slot reuse gate update rule: only a successfully submitted
	// timeline value may become the gate a later frame waits on.
	[[nodiscard]] uint64_t UpdateSlotReuseGate(uint64_t previousGate,
		VkResult submitResult, uint64_t candidateValue) noexcept;

	// Runtime health distinguishes "the runtime cannot continue" (fatal,
	// e.g. a surface-lost present or an out-of-memory result) from "the
	// VkDevice itself is lost". Only VK_ERROR_DEVICE_LOST marks the device
	// lost; every other fatal error still allows a normal GPU quiesce during
	// cleanup.
	[[nodiscard]] bool IsVulkanDeviceLostError(VkResult result) noexcept;

	struct VulkanRuntimeHealthState
	{
		bool m_Fatal = false;
		bool m_DeviceLost = false;
	};

	[[nodiscard]] VulkanRuntimeHealthState UpdateVulkanRuntimeHealth(
		const VulkanRuntimeHealthState& state, VkResult error) noexcept;

	// The frame-slot reuse gate wait outcome: only a successful timeline
	// wait may continue to command-pool reset and acquire; any failure stops
	// BeginFrame before those steps.
	enum class VulkanBeginGateOutcome : uint8_t
	{
		Ready,
		Fatal,
	};

	[[nodiscard]] VulkanBeginGateOutcome ClassifyVulkanBeginGateResult(VkResult waitResult) noexcept;

	// Frame-slot ring selection and frame pairing bookkeeping. The
	// imageAvailable semaphore identity always follows the frame slot and the
	// renderingFinished semaphore identity always follows the swapchain image
	// index; the two domains never cross.
	class VulkanFrameIndexModel
	{
	public:
		explicit VulkanFrameIndexModel(uint32_t frameSlotCount) noexcept;

		[[nodiscard]] uint32_t GetFrameSlotCount() const noexcept { return m_FrameSlotCount; }
		// Ring selection: next reusable frame slot.
		[[nodiscard]] uint32_t NextFrameSlot() noexcept;
		void CommitFrame(uint32_t frameSlot, uint32_t backBufferIndex) noexcept;
		void ResetFramePairs() noexcept;
		[[nodiscard]] const std::vector<std::pair<uint32_t, uint32_t>>& GetFramePairs() const noexcept
		{
			return m_FramePairs;
		}

	private:
		uint32_t m_FrameSlotCount = 1;
		uint32_t m_NextSlotIndex = 0;
		std::vector<std::pair<uint32_t, uint32_t>> m_FramePairs;
	};

	// Per-swapchain-image tracked layout state. New or recreated images start
	// Undefined; a successful present leaves the image Present.
	class VulkanImageLayoutTracker
	{
	public:
		void Reset(uint32_t imageCount) noexcept;
		[[nodiscard]] VulkanPresentImageLayout Get(uint32_t image) const noexcept;
		void Set(uint32_t image, VulkanPresentImageLayout layout) noexcept;
		[[nodiscard]] uint32_t GetImageCount() const noexcept
		{
			return static_cast<uint32_t>(m_Layouts.size());
		}

	private:
		std::vector<VulkanPresentImageLayout> m_Layouts;
	};

	// Active-frame transaction state per frame slot. An acquired frame must
	// terminate in exactly one of End or Abort; illegal transitions are
	// rejected.
	class VulkanFrameSlotStateMachine
	{
	public:
		void Reset(uint32_t slotCount) noexcept;
		[[nodiscard]] bool TryBegin(uint32_t slot) noexcept;
		[[nodiscard]] bool TryEnd(uint32_t slot) noexcept;
		[[nodiscard]] bool TryAbort(uint32_t slot) noexcept;
		[[nodiscard]] bool IsActive(uint32_t slot) const noexcept;
		[[nodiscard]] VulkanFrameSlotPhase GetPhase(uint32_t slot) const noexcept;
		[[nodiscard]] uint32_t GetSlotCount() const noexcept
		{
			return static_cast<uint32_t>(m_Phases.size());
		}

	private:
		std::vector<VulkanFrameSlotPhase> m_Phases;
	};

	struct VulkanFrameSlot
	{
		uint32_t m_Index = 0;
		VkSemaphore m_ImageAvailable = VK_NULL_HANDLE;
		VkCommandPool m_CommandPool = VK_NULL_HANDLE;
		VkCommandBuffer m_NormalCommandBuffer = VK_NULL_HANDLE;
		VkCommandBuffer m_AbortCommandBuffer = VK_NULL_HANDLE;
		uint64_t m_LastSubmittedTimelineValue = 0;
	};

	struct VulkanFrameRuntimeCreateInfo
	{
		// All borrowed: the caller owns the bootstrap-created objects and
		// destroys them after the frame runtime.
		VulkanInstance* m_Instance = nullptr;
		VulkanWin32Surface* m_Surface = nullptr;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VulkanDevice* m_Device = nullptr;
		const VulkanAdapterCapabilitySnapshot* m_Snapshot = nullptr;
		uint32_t m_FrameSlotCount = 2;
		RHIFormat m_RequestedFormat = RHIFormat::R8G8B8A8Unorm;
		bool m_Vsync = false;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
	};

	// The exact acquired pair of one active frame. The public RHI contract
	// allows at most one active frame; EndFrame/AbortFrame consume the pair
	// bound here and never accept caller-supplied indices.
	struct VulkanActiveFrame
	{
		uint32_t m_FrameSlotIndex = 0;
		uint32_t m_BackBufferIndex = 0;
	};

	struct VulkanBeginFrameResult
	{
		VulkanAcquireOutcome m_Status = VulkanAcquireOutcome::Fatal;
		// Valid when m_Status is Acquired or RecreatePending.
		uint32_t m_FrameSlotIndex = 0;
		uint32_t m_BackBufferIndex = 0;
		// True when the acquire reported SUBOPTIMAL: the image is valid and
		// the swapchain should be recreated at a safe point.
		bool m_RecreatePending = false;
		VkResult m_Result = VK_SUCCESS;

		[[nodiscard]] bool IsAcquired() const noexcept
		{
			return m_Status == VulkanAcquireOutcome::Acquired ||
				m_Status == VulkanAcquireOutcome::RecreatePending;
		}
	};

	struct VulkanSubmitPresentResult
	{
		// True when the graphics submission was accepted and the timeline
		// signal plus the frame-slot reuse gate are valid.
		bool m_Submitted = false;
		// True when the present call was made and reported a non-fatal
		// result; the frame transaction is complete.
		bool m_Presented = false;
		// True when the present reported SUBOPTIMAL/OUT_OF_DATE: recreation
		// is scheduled at the next safe point.
		bool m_RecreatePending = false;
		// True when the runtime entered the fatal state; no further
		// BeginFrame is allowed.
		bool m_Fatal = false;
		VkResult m_Result = VK_SUCCESS;
	};

	// Minimal swapchain frame lifecycle: acquire, record a known-color clear,
	// submit, signal the graphics timeline and the per-image rendering
	// finished semaphore, present. AbortFrame releases an acquired image with
	// a dedicated minimal command buffer; the partially recorded application
	// command buffer is never submitted. The runtime is single-owner on one
	// graphics/present queue, which satisfies Vulkan queue external
	// synchronization. Only one frame may be active at a time, and the
	// acquired (frameSlotIndex, backBufferIndex) pair is bound internally:
	// EndFrame/AbortFrame consume the active pair and never accept
	// caller-supplied indices.
	class VulkanFrameRuntime
	{
	public:
		struct Result
		{
			std::unique_ptr<VulkanFrameRuntime> m_Runtime;
			std::string m_Error;
			VkResult m_Result = VK_SUCCESS;

			[[nodiscard]] bool Succeeded() const noexcept { return m_Runtime != nullptr; }
		};

	public:
		VulkanFrameRuntime() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanFrameRuntime);
		~VulkanFrameRuntime();

		[[nodiscard]] static Result Create(const VulkanFrameRuntimeCreateInfo& createInfo) noexcept;

		// Acquires the next image. The caller must not call BeginFrame while
		// the drawable extent is zero. An OUT_OF_DATE result hands over no
		// image: the caller owns the drawable extent and must recreate the
		// swapchain with the real extent before retrying.
		[[nodiscard]] VulkanBeginFrameResult BeginFrame() noexcept;
		// Records the known-color clear, submits and presents the active
		// frame.
		[[nodiscard]] VulkanSubmitPresentResult EndFrame(
			const std::array<float, 4>& clearColor) noexcept;
		// Releases the active frame's image with the dedicated abort command
		// buffer.
		[[nodiscard]] VulkanSubmitPresentResult AbortFrame() noexcept;

		// Safe-point swapchain recreation (requires no active frame).
		[[nodiscard]] bool RecreateSwapChain(uint32_t width, uint32_t height,
			bool vsync, std::string& outError) noexcept;
		void SetVsync(bool vsync) noexcept { m_Vsync = vsync; }
		[[nodiscard]] bool GetVsync() const noexcept { return m_Vsync; }

		// Explicit quiesce: waits for the graphics queue and the committed
		// timeline before releasing GPU-owned children. The destructor calls
		// it defensively so early-return failure paths (including partial
		// construction) never destroy command pools or semaphores that are
		// still in flight. Quiesce is skipped only when the VkDevice itself
		// is lost; an ordinary fatal error still quiesces.
		void Finalize() noexcept;
		// Waits for the graphics queue and the last committed timeline
		// value. Returns VK_SUCCESS or the first failed wait result.
		[[nodiscard]] VkResult WaitIdle() noexcept;

		[[nodiscard]] bool IsFatal() const noexcept { return m_Fatal; }
		[[nodiscard]] bool IsDeviceLost() const noexcept { return m_DeviceLost; }
		[[nodiscard]] bool HasActiveFrame() const noexcept { return m_ActiveFrame.has_value(); }

		[[nodiscard]] VulkanSwapChain& GetSwapChain() noexcept { return *m_SwapChain; }
		[[nodiscard]] const VulkanSwapChain& GetSwapChain() const noexcept { return *m_SwapChain; }
		[[nodiscard]] const VulkanTimelineFence& GetTimeline() const noexcept { return *m_Timeline; }
		[[nodiscard]] uint64_t GetTimelineSignalValue() const noexcept
		{
			return m_Timeline ? m_Timeline->GetCurrentSignalValue() : 0;
		}
		[[nodiscard]] const VulkanFrameIndexModel& GetIndexModel() const noexcept
		{
			return m_IndexModel;
		}
		[[nodiscard]] const VulkanImageLayoutTracker& GetLayoutTracker() const noexcept
		{
			return m_LayoutTracker;
		}
		[[nodiscard]] const VulkanFrameSlotStateMachine& GetStateMachine() const noexcept
		{
			return m_StateMachine;
		}
		[[nodiscard]] uint32_t GetFrameSlotCount() const noexcept
		{
			return static_cast<uint32_t>(m_FrameSlots.size());
		}
		// Diagnostic-only accessors for the semaphore identity contract: the
		// imageAvailable semaphore always follows the frame slot and the
		// renderingFinished semaphore always follows the swapchain image.
		[[nodiscard]] VkSemaphore GetImageAvailableSemaphore(uint32_t frameSlotIndex) const noexcept;
		[[nodiscard]] VkSemaphore GetRenderingFinishedSemaphore(uint32_t backBufferIndex) const noexcept;

	private:
		void RecordNormalFrame(
			VkCommandBuffer commandBuffer, uint32_t backBufferIndex,
			const std::array<float, 4>& clearColor) noexcept;
		void RecordAbortFrame(VkCommandBuffer commandBuffer, uint32_t backBufferIndex) noexcept;
		[[nodiscard]] VulkanSubmitPresentResult SubmitAndPresent(
			uint32_t frameSlotIndex, uint32_t backBufferIndex,
			VkCommandBuffer commandBuffer) noexcept;
		void MarkFatal(VkResult error) noexcept;
		void DestroyFrameSlots() noexcept;

		VulkanDevice* m_Device = nullptr;
		std::unique_ptr<VulkanTimelineFence> m_Timeline;
		std::unique_ptr<VulkanSwapChain> m_SwapChain;
		std::vector<VulkanFrameSlot> m_FrameSlots;
		VulkanFrameIndexModel m_IndexModel{ 2 };
		VulkanImageLayoutTracker m_LayoutTracker;
		VulkanFrameSlotStateMachine m_StateMachine;
		std::optional<VulkanActiveFrame> m_ActiveFrame;
		bool m_Vsync = false;
		bool m_Fatal = false;
		bool m_DeviceLost = false;
		bool m_Finalized = false;
	};
}
