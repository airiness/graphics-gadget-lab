#include "Graphics/RHI/Vulkan/VulkanTimelineFence.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <vector>

namespace gglab
{
	namespace
	{
		// Backend-neutral fence identity: a process-stable monotonic index.
		// Mirrors the DX12 fence handle allocation so RHIFencePoints stay
		// comparable across backends without a shared handle table.
		[[nodiscard]] RHIFenceHandle AllocateRHIFenceHandle() noexcept
		{
			static std::atomic<RHIFenceHandle::IndexType> nextIndex = 0;
			return RHIFenceHandle(nextIndex.fetch_add(1, std::memory_order_relaxed), 1);
		}
	}

	VkResult WaitVulkanQueuesIdle(VkDevice device, std::span<VulkanQueueIdleSignal> signals,
		uint32_t timeoutMilliseconds, const VulkanQueueIdleDispatch& dispatch) noexcept
	{
		const auto started = std::chrono::steady_clock::now();
		if (device == VK_NULL_HANDLE || signals.empty() ||
			!dispatch.m_QueueSubmit2 || !dispatch.m_WaitSemaphores)
		{
			return VK_ERROR_INITIALIZATION_FAILED;
		}
		for (VulkanQueueIdleSignal& signal : signals)
		{
			signal.m_Submitted = false;
			if (signal.m_Queue == VK_NULL_HANDLE || signal.m_Timeline == VK_NULL_HANDLE ||
				signal.m_Value == 0)
			{
				return VK_ERROR_INITIALIZATION_FAILED;
			}
		}
		std::vector<VkSemaphore> semaphores;
		std::vector<uint64_t> values;
		semaphores.reserve(signals.size());
		values.reserve(signals.size());
		for (VulkanQueueIdleSignal& signal : signals)
		{
			const VkSemaphoreSubmitInfo signalInfo{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = signal.m_Timeline,
				.value = signal.m_Value,
				.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			};
			const VkSubmitInfo2 submitInfo{
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
				.signalSemaphoreInfoCount = 1,
				.pSignalSemaphoreInfos = &signalInfo,
			};
			const VkResult result = dispatch.m_QueueSubmit2(
				signal.m_Queue, 1, &submitInfo, VK_NULL_HANDLE);
			if (result != VK_SUCCESS)
			{
				// Never wait on the failed marker. Earlier successful markers stay committed
				// by the caller, and a later retry can cover the uncertain work with new ones.
				return result;
			}
			signal.m_Submitted = true;
			semaphores.push_back(signal.m_Timeline);
			values.push_back(signal.m_Value);
		}
		uint64_t remaining = ToVulkanTimeoutNanoseconds(timeoutMilliseconds);
		if (timeoutMilliseconds != RHIInfiniteWaitMilliseconds)
		{
			const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count();
			remaining -= std::min(remaining, static_cast<uint64_t>(elapsed));
		}
		const VkSemaphoreWaitInfo waitInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.semaphoreCount = static_cast<uint32_t>(semaphores.size()),
			.pSemaphores = semaphores.data(),
			.pValues = values.data(),
		};
		return dispatch.m_WaitSemaphores(device, &waitInfo, remaining);
	}

	VulkanTimelineFence::~VulkanTimelineFence()
	{
		Destroy();
	}

	VulkanTimelineFence::Result VulkanTimelineFence::Create(const CreateInfo& createInfo) noexcept
	{
		Result result{};
		if (createInfo.m_Device == VK_NULL_HANDLE)
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			result.m_Error = "VulkanTimelineFence requires a valid device.";
			return result;
		}

		auto fence = std::make_unique<VulkanTimelineFence>();
		fence->m_Device = createInfo.m_Device;
		fence->m_RHIHandle = AllocateRHIFenceHandle();

		VkSemaphoreTypeCreateInfo typeCreateInfo{};
		typeCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		typeCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		typeCreateInfo.initialValue = 0;

		VkSemaphoreCreateInfo createInfo2{};
		createInfo2.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		createInfo2.pNext = &typeCreateInfo;

		const VkResult createResult = vkCreateSemaphore(
			createInfo.m_Device, &createInfo2, nullptr, &fence->m_Semaphore);
		if (createResult != VK_SUCCESS)
		{
			result.m_Result = createResult;
			result.m_Error = std::format(
				"vkCreateSemaphore (timeline) failed with {}.", ToString(createResult));
			return result;
		}

		result.m_Fence = std::move(fence);
		return result;
	}

	VkResult VulkanTimelineFence::Wait(uint64_t value, uint64_t timeoutNanoseconds) const noexcept
	{
		if (m_Semaphore == VK_NULL_HANDLE)
		{
			return VK_ERROR_INITIALIZATION_FAILED;
		}
		uint64_t completedValue = 0;
		const VkResult counterResult = GetCompletedValue(completedValue);
		if (counterResult != VK_SUCCESS)
		{
			return counterResult;
		}
		if (completedValue >= value)
		{
			return VK_SUCCESS;
		}
		const VkSemaphoreWaitInfo waitInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.semaphoreCount = 1,
			.pSemaphores = &m_Semaphore,
			.pValues = &value,
		};
		return vkWaitSemaphores(m_Device, &waitInfo, timeoutNanoseconds);
	}

	VkResult VulkanTimelineFence::GetCompletedValue(uint64_t& outValue) const noexcept
	{
		if (m_Semaphore == VK_NULL_HANDLE)
		{
			return VK_ERROR_INITIALIZATION_FAILED;
		}
		uint64_t value = 0;
		const VkResult result = vkGetSemaphoreCounterValue(m_Device, m_Semaphore, &value);
		if (result != VK_SUCCESS)
		{
			return result;
		}
		outValue = value;
		return VK_SUCCESS;
	}

	void VulkanTimelineFence::Destroy() noexcept
	{
		if (m_Semaphore != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(m_Device, m_Semaphore, nullptr);
			m_Semaphore = VK_NULL_HANDLE;
		}
		m_Device = VK_NULL_HANDLE;
	}
}
