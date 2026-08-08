#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace gglab
{
	// Graphics-queue timeline semaphore used as the frame-slot reuse gate.
	// The timeline value is monotonic: every successful QueueSubmit2 (normal
	// or abort) signals the next value, and a frame slot waits on its own
	// last submitted value before it is reused.
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

		// Allocates the next monotonic signal value for a submission.
		[[nodiscard]] uint64_t AllocateSignalValue() noexcept { return ++m_NextValue; }
		[[nodiscard]] uint64_t GetCurrentSignalValue() const noexcept { return m_NextValue; }

		// Blocks until the graphics timeline reaches the given value.
		void Wait(uint64_t value) const noexcept;
		[[nodiscard]] uint64_t GetCompletedValue() const noexcept;

		[[nodiscard]] VkSemaphore Get() const noexcept { return m_Semaphore; }

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkSemaphore m_Semaphore = VK_NULL_HANDLE;
		uint64_t m_NextValue = 0;
	};
}
