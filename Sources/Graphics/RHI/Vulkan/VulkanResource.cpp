#include "Core/Precompiled.h"
#include "Graphics/RHI/Vulkan/VulkanResource.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

namespace gglab
{
	void VulkanBuffer::Create(const CreateInfo& createInfo) noexcept
	{
		GGLAB_ASSERT_MSG(createInfo.m_Allocator != VK_NULL_HANDLE,
			"VulkanBuffer requires a VMA allocator.");
		GGLAB_ASSERT_MSG(createInfo.m_Device != VK_NULL_HANDLE,
			"VulkanBuffer requires a logical device.");

		if (IsValid())
		{
			Release();
		}

		m_Allocator = createInfo.m_Allocator;
		m_Device = createInfo.m_Device;
		m_CreateInfo = createInfo.m_CreateInfo;
		m_InitialState = createInfo.m_InitialState;
		m_MemoryUsage = createInfo.m_MemoryUsage;

		VmaAllocationInfo* allocationInfo = nullptr;
		VmaAllocationInfo allocationInfoStorage{};
		if ((createInfo.m_AllocationCreateInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) != 0)
		{
			allocationInfo = &allocationInfoStorage;
		}
		const VkResult createResult = vmaCreateBuffer(m_Allocator, &m_CreateInfo,
			&createInfo.m_AllocationCreateInfo, &m_Buffer, &m_Allocation, allocationInfo);
		if (createResult != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanBuffer::Create failed to allocate the native buffer ({}).",
				ToString(createResult));
			Release();
			return;
		}
		if (allocationInfo != nullptr)
		{
			// Persistently mapped host-visible allocation: the mapping stays
			// valid until the allocation is destroyed.
			m_MappedData = allocationInfo->pMappedData;
		}
	}

	void VulkanBuffer::AdoptExternal(
		VkDevice device, VkBuffer buffer, RHIResourceState initialState) noexcept
	{
		if (IsValid())
		{
			Release();
		}

		m_Device = device;
		m_Allocator = VK_NULL_HANDLE;
		m_Allocation = VK_NULL_HANDLE;
		m_Borrowed = true;
		m_Buffer = buffer;
		m_InitialState = initialState;
		m_MemoryUsage = RHIMemoryUsage::GpuOnly;
		m_CreateInfo = {};
		m_MappedData = nullptr;
	}

	void VulkanBuffer::Release() noexcept
	{
		if (m_Allocation != VK_NULL_HANDLE)
		{
			if (m_MappedData != nullptr)
			{
				vmaUnmapMemory(m_Allocator, m_Allocation);
				m_MappedData = nullptr;
			}
			vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);
		}
		else if (m_Buffer != VK_NULL_HANDLE && !m_Borrowed)
		{
			// Owned resource whose allocation failed: destroy the handle only.
			vkDestroyBuffer(m_Device, m_Buffer, nullptr);
		}
		m_Buffer = VK_NULL_HANDLE;
		m_Allocation = VK_NULL_HANDLE;
		m_Allocator = VK_NULL_HANDLE;
		m_Borrowed = false;
		m_CreateInfo = {};
		m_InitialState = {};
	}

	void VulkanBuffer::SetDebugName(const char* name) noexcept
	{
		if (m_Buffer != VK_NULL_HANDLE)
		{
			SetVulkanObjectDebugName(m_Device, VK_OBJECT_TYPE_BUFFER,
				reinterpret_cast<uint64_t>(m_Buffer), name);
		}
	}

	void VulkanTexture::Create(const CreateInfo& createInfo) noexcept
	{
		GGLAB_ASSERT_MSG(createInfo.m_Allocator != VK_NULL_HANDLE,
			"VulkanTexture requires a VMA allocator.");
		GGLAB_ASSERT_MSG(createInfo.m_Device != VK_NULL_HANDLE,
			"VulkanTexture requires a logical device.");

		if (IsValid())
		{
			Release();
		}

		m_Allocator = createInfo.m_Allocator;
		m_Device = createInfo.m_Device;
		m_CreateInfo = createInfo.m_CreateInfo;
		m_InitialState = createInfo.m_InitialState;

		const VkResult createResult = vmaCreateImage(m_Allocator, &m_CreateInfo,
			&createInfo.m_AllocationCreateInfo, &m_Image, &m_Allocation, nullptr);
		if (createResult != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTexture::Create failed to allocate the native image ({}).",
				ToString(createResult));
			Release();
		}
	}

	void VulkanTexture::AdoptExternal(
		VkDevice device, VkImage image, RHIResourceState initialState) noexcept
	{
		if (IsValid())
		{
			Release();
		}

		m_Device = device;
		m_Allocator = VK_NULL_HANDLE;
		m_Allocation = VK_NULL_HANDLE;
		m_Borrowed = true;
		m_Image = image;
		m_InitialState = initialState;
		m_CreateInfo = {};
	}

	void VulkanTexture::Release() noexcept
	{
		if (m_Allocation != VK_NULL_HANDLE)
		{
			vmaDestroyImage(m_Allocator, m_Image, m_Allocation);
		}
		else if (m_Image != VK_NULL_HANDLE && !m_Borrowed)
		{
			// Owned resource whose allocation failed: destroy the handle only.
			vkDestroyImage(m_Device, m_Image, nullptr);
		}
		m_Image = VK_NULL_HANDLE;
		m_Allocation = VK_NULL_HANDLE;
		m_Allocator = VK_NULL_HANDLE;
		m_Borrowed = false;
		m_CreateInfo = {};
		m_InitialState = {};
	}

	void VulkanTexture::SetDebugName(const char* name) noexcept
	{
		if (m_Image != VK_NULL_HANDLE)
		{
			SetVulkanObjectDebugName(m_Device, VK_OBJECT_TYPE_IMAGE,
				reinterpret_cast<uint64_t>(m_Image), name);
		}
	}
}
