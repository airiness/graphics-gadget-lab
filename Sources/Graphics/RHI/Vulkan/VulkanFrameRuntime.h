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
	// over an image; OUT_OF_DATE and fatal results are separated.
	enum class VulkanAcquireOutcome : uint8_t
	{
		Acquired,
		RecreatePending,   // SUBOPTIMAL: image valid, schedule recreate
		OutOfDateRetry,    // OUT_OF_DATE: recreate and retry
		Fatal,
	};

	// Classifies vkQueuePresentKHR results. OUT_OF_DATE is not fatal: the
	// submission already happened and the frame-slot timeline stays valid.
	enum class VulkanPresentOutcome : uint8_t
	{
		Presented,
		RecreatePending,   // SUBOPTIMAL or OUT_OF_DATE: recreate at safe point
		Failed,
	};

	[[nodiscard]] VulkanAcquireOutcome ClassifyVulkanAcquireResult(VkResult result) noexcept;
	[[nodiscard]] VulkanPresentOutcome ClassifyVulkanPresentResult(VkResult result) noexcept;

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

	struct VulkanBeginFrameResult
	{
		bool m_Acquired = false;
		uint32_t m_FrameSlotIndex = 0;
		uint32_t m_BackBufferIndex = 0;
		bool m_RecreatePending = false;
	};

	// Minimal swapchain frame lifecycle: acquire, record a known-color clear,
	// submit, signal the graphics timeline and the per-image rendering
	// finished semaphore, present. AbortFrame releases an acquired image with
	// a dedicated minimal command buffer; the partially recorded application
	// command buffer is never submitted. The runtime is single-owner on one
	// graphics/present queue, which satisfies Vulkan queue external
	// synchronization.
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
		// the drawable extent is zero.
		[[nodiscard]] VulkanBeginFrameResult BeginFrame() noexcept;
		// Records the known-color clear, submits and presents.
		void EndFrame(uint32_t frameSlotIndex, uint32_t backBufferIndex,
			const std::array<float, 4>& clearColor) noexcept;
		// Releases the acquired image with the dedicated abort command buffer.
		void AbortFrame(uint32_t frameSlotIndex, uint32_t backBufferIndex) noexcept;

		// Safe-point swapchain recreation (requires no active frame).
		[[nodiscard]] bool RecreateSwapChain(uint32_t width, uint32_t height,
			bool vsync, std::string& outError) noexcept;
		void SetVsync(bool vsync) noexcept { m_Vsync = vsync; }
		[[nodiscard]] bool GetVsync() const noexcept { return m_Vsync; }

		void WaitIdle() noexcept;

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
		void SubmitAndPresent(uint32_t frameSlotIndex, uint32_t backBufferIndex,
			VkCommandBuffer commandBuffer) noexcept;
		void DestroyFrameSlots() noexcept;

		VulkanDevice* m_Device = nullptr;
		std::unique_ptr<VulkanTimelineFence> m_Timeline;
		std::unique_ptr<VulkanSwapChain> m_SwapChain;
		std::vector<VulkanFrameSlot> m_FrameSlots;
		VulkanFrameIndexModel m_IndexModel{ 2 };
		VulkanImageLayoutTracker m_LayoutTracker;
		VulkanFrameSlotStateMachine m_StateMachine;
		bool m_Vsync = false;
	};
}
