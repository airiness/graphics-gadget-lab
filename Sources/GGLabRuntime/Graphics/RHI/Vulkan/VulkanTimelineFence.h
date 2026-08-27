#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/RHI/RHIFence.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace gglab
{
	// Logical-queue timeline semaphore. The graphics instance is used as the
	// frame-slot reuse gate; the transfer instance gates upload publication and
	// staging retirement.
	// The timeline value is monotonic: every successfully submitted
	// QueueSubmit2 (normal or abort) signals the next value, and a frame
	// slot waits on its own last successfully submitted value before it is
	// reused. Values are reserved as candidates and only committed when the
	// submission succeeded; a failed submission may leave an unused gap but
	// the gate never waits on a value that was not submitted.
	//
	// The fence owns a stable backend-neutral RHIFenceHandle so submitted
	// timeline values can form valid RHIFencePoints for resource last-use
	// tracking and deferred destruction.
	class VulkanTimelineFence
	{
	public:
		struct CreateInfo
		{
			VkDevice m_Device = VK_NULL_HANDLE;
		};

		struct Result
		{
			std::unique_ptr<VulkanTimelineFence> m_Fence;
			std::string m_Error;
			VkResult m_Result = VK_SUCCESS;

			[[nodiscard]] bool Succeeded() const noexcept { return m_Fence != nullptr; }
		};

	public:
		VulkanTimelineFence() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanTimelineFence);
		~VulkanTimelineFence();

		[[nodiscard]] static Result Create(const CreateInfo& createInfo) noexcept;

		// Reserves the next monotonic candidate value for a submission. The
		// value only becomes a valid wait target after CommitSubmittedValue.
		[[nodiscard]] uint64_t ReserveSignalValue() noexcept { return ++m_ReservedValue; }
		// Commits a reserved value as actually submitted. Monotonic: a failed
		// submission leaves an unused gap and the committed value stays the
		// last successfully submitted one.
		void CommitSubmittedValue(uint64_t value) noexcept
		{
			m_SubmittedValue = std::max(m_SubmittedValue, value);
		}
		// The last successfully submitted value; waiting at or below it is
		// always safe.
		[[nodiscard]] uint64_t GetCurrentSignalValue() const noexcept { return m_SubmittedValue; }

		// The stable backend-neutral identity of this timeline; combined
		// with a committed value it forms a valid RHIFencePoint.
		[[nodiscard]] RHIFenceHandle GetRHIHandle() const noexcept { return m_RHIHandle; }

		// Blocks until the timeline reaches the given value. The
		// caller must only wait on committed values. Returns VK_SUCCESS or
		// the Vulkan error (e.g. VK_ERROR_DEVICE_LOST); the result is never
		// swallowed.
		[[nodiscard]] VkResult Wait(uint64_t value) const noexcept;
		// Reads the current timeline counter. Returns VK_SUCCESS or the
		// Vulkan error; outValue is set only on success.
		[[nodiscard]] VkResult GetCompletedValue(uint64_t& outValue) const noexcept;

		[[nodiscard]] VkSemaphore Get() const noexcept { return m_Semaphore; }

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkSemaphore m_Semaphore = VK_NULL_HANDLE;
		RHIFenceHandle m_RHIHandle{};
		uint64_t m_ReservedValue = 0;
		uint64_t m_SubmittedValue = 0;
	};
}
