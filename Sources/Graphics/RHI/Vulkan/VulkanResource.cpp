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
			// VMA keeps this mapping for the allocation lifetime; it must
			// never be passed to vmaUnmapMemory and is torn down by
			// vmaDestroyBuffer.
			m_Mapping = VulkanBufferMapping::Persistent;
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
		m_Mapping = VulkanBufferMapping::None;
		m_MappedData = nullptr;
	}

	void VulkanBuffer::Release() noexcept
	{
		if (m_Allocation != VK_NULL_HANDLE)
		{
			if (m_Mapping == VulkanBufferMapping::Explicit && m_MappedData != nullptr)
			{
				// Only explicit mappings carry an unmap obligation; the
				// persistent VMA mapping is released by vmaDestroyBuffer.
				vmaUnmapMemory(m_Allocator, m_Allocation);
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
		m_Mapping = VulkanBufferMapping::None;
		m_MappedData = nullptr;
	}

	void VulkanBuffer::SetDebugName(const char* name) noexcept
	{
		if (m_Buffer != VK_NULL_HANDLE)
		{
			SetVulkanObjectDebugName(m_Device, VK_OBJECT_TYPE_BUFFER,
				reinterpret_cast<uint64_t>(m_Buffer), name);
		}
	}

	void* VulkanBuffer::Map(RHIMappedBufferRange readRange) noexcept
	{
		if (m_Buffer == VK_NULL_HANDLE)
		{
			return nullptr;
		}
		if (m_Borrowed || m_Allocation == VK_NULL_HANDLE)
		{
			// Borrowed buffers carry no allocation metadata: the backend
			// cannot prove host visibility, flush or invalidate ranges, so
			// backend mapping is rejected. A future contract extension
			// would require the caller to supply known mapping metadata.
			return nullptr;
		}

		if (m_Mapping == VulkanBufferMapping::Persistent)
		{
			// The VMA-owned pointer is used directly; readback buffers
			// refresh the read range from device memory.
			if (m_MemoryUsage == RHIMemoryUsage::GpuToCpu &&
				readRange.m_End > readRange.m_Begin)
			{
				vmaInvalidateAllocation(m_Allocator, m_Allocation,
					readRange.m_Begin, readRange.m_End - readRange.m_Begin);
			}
			return m_MappedData;
		}

		void* mappedData = nullptr;
		const VkResult mapResult =
			vmaMapMemory(m_Allocator, m_Allocation, &mappedData);
		if (mapResult != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_WARN("VulkanBuffer::Map failed with {}.", ToString(mapResult));
			return nullptr;
		}
		m_Mapping = VulkanBufferMapping::Explicit;
		m_MappedData = mappedData;
		if (m_MemoryUsage == RHIMemoryUsage::GpuToCpu && readRange.m_End > readRange.m_Begin)
		{
			vmaInvalidateAllocation(m_Allocator, m_Allocation,
				readRange.m_Begin, readRange.m_End - readRange.m_Begin);
		}
		return mappedData;
	}

	void VulkanBuffer::Unmap(RHIMappedBufferRange writtenRange) noexcept
	{
		if (m_MappedData == nullptr)
		{
			return;
		}

		// Upload buffers flush the written range so the GPU sees the host
		// writes.
		if (m_MemoryUsage == RHIMemoryUsage::CpuToGpu &&
			writtenRange.m_End > writtenRange.m_Begin)
		{
			vmaFlushAllocation(m_Allocator, m_Allocation,
				writtenRange.m_Begin, writtenRange.m_End - writtenRange.m_Begin);
		}

		if (m_Mapping == VulkanBufferMapping::Explicit)
		{
			vmaUnmapMemory(m_Allocator, m_Allocation);
			m_Mapping = VulkanBufferMapping::None;
			m_MappedData = nullptr;
		}
		// Persistent mappings keep their VMA-owned pointer.
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
			return;
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
		// No native creation history is fabricated for imported images;
		// the authoritative RHI description lives in the resource
		// manager slot.
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
