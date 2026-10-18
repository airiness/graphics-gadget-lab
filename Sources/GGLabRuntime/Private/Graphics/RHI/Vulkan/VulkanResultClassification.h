#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHIFrameSubmission.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace gglab
{
	// Checked results of the native wait, query and submit calls. Only
	// VK_ERROR_DEVICE_LOST confirms device loss; only the out-of-memory
	// results of vkQueueSubmit2 guarantee that the submit had no effect.
	[[nodiscard]] constexpr RHIGpuWaitResult ClassifyVulkanWaitResult(VkResult result) noexcept
	{
		switch (result)
		{
		case VK_SUCCESS:
			return RHIGpuWaitResult::Completed;
		case VK_TIMEOUT:
			return RHIGpuWaitResult::Timeout;
		case VK_ERROR_DEVICE_LOST:
			return RHIGpuWaitResult::DeviceLost;
		default:
			return RHIGpuWaitResult::Failed;
		}
	}

	[[nodiscard]] constexpr RHIFenceQueryResult ClassifyVulkanFenceQuery(
		VkResult result, uint64_t completedValue, uint64_t targetValue) noexcept
	{
		if (targetValue == 0)
		{
			return RHIFenceQueryResult::Failed;
		}
		if (result == VK_ERROR_DEVICE_LOST)
		{
			return RHIFenceQueryResult::DeviceLost;
		}
		if (result != VK_SUCCESS)
		{
			return RHIFenceQueryResult::Failed;
		}
		return completedValue >= targetValue ?
			RHIFenceQueryResult::Completed :
			RHIFenceQueryResult::Pending;
	}

	[[nodiscard]] constexpr RHISubmitEffect ClassifyVulkanSubmitEffect(
		bool submitIssued, VkResult result) noexcept
	{
		return !submitIssued || result == VK_ERROR_OUT_OF_HOST_MEMORY ||
				result == VK_ERROR_OUT_OF_DEVICE_MEMORY ?
			RHISubmitEffect::NoEffect :
			RHISubmitEffect::Accepted;
	}
}
