#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace gglab
{
	// Graphics-queue timeline semaphore used as the frame-slot reuse gate.
	// The timeline value is monotonic: every successfully submitted
	// QueueSubmit2 (normal or abort) signals the next value, and a frame
	// slot waits on its own last successfully submitted value before it is
	// reused. Values are reserved as candidates and only committed when the
	// submission succeeded; a failed submission may leave an unused gap but
	// the gate never waits on a value that was not submitted.
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

		// Blocks until the graphics timeline reaches the given value. The
		// caller must only wait on committed values.
		void Wait(uint64_t value) const noexcept;
		[[nodiscard]] uint64_t GetCompletedValue() const noexcept;

		[[nodiscard]] VkSemaphore Get() const noexcept { return m_Semaphore; }

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkSemaphore m_Semaphore = VK_NULL_HANDLE;
		uint64_t m_ReservedValue = 0;
		uint64_t m_SubmittedValue = 0;
	};
}
