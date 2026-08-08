#include "Core/Precompiled.h"
#include "Graphics/RHI/Vulkan/VulkanTimelineFence.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <array>
#include <format>

namespace gglab
{
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

	void VulkanTimelineFence::Wait(uint64_t value) const noexcept
	{
		if (m_Semaphore == VK_NULL_HANDLE || GetCompletedValue() >= value)
		{
			return;
		}
		const VkSemaphoreWaitInfo waitInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.semaphoreCount = 1,
			.pSemaphores = &m_Semaphore,
			.pValues = &value,
		};
		vkWaitSemaphores(m_Device, &waitInfo, UINT64_MAX);
	}

	uint64_t VulkanTimelineFence::GetCompletedValue() const noexcept
	{
		if (m_Semaphore == VK_NULL_HANDLE)
		{
			return 0;
		}
		uint64_t value = 0;
		vkGetSemaphoreCounterValue(m_Device, m_Semaphore, &value);
		return value;
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
