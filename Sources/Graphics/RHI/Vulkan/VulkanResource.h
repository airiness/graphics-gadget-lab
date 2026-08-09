#pragma once
#include "Graphics/RHI/RHIResource.h"
#include "Graphics/RHI/RHITypes.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace gglab
{
	// Shared VMA-backed native resource base. Owned resources hold a VMA
	// allocation; imported (borrowed) resources adopt a native handle with
	// no allocation, matching the swapchain-image ownership contract. The
	// logical initial RHI state is preserved so the resource layer never
	// guesses how a resource was born.
	class VulkanResource
	{
	public:
		VulkanResource() noexcept = default;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(VulkanResource);
		virtual ~VulkanResource() = default;

		[[nodiscard]] virtual bool IsValid() const noexcept = 0;
		[[nodiscard]] bool IsExternal() const noexcept { return m_Borrowed; }
		[[nodiscard]] bool OwnsAllocation() const noexcept { return !m_Borrowed; }
		[[nodiscard]] VmaAllocation GetAllocation() const noexcept { return m_Allocation; }

		// Applies the debug name through VK_EXT_debug_utils when available.
		virtual void SetDebugName(const char* name) noexcept = 0;
		// Releases the native handle and, when owned, the VMA allocation.
		virtual void Release() noexcept = 0;

	protected:
		VkDevice m_Device = VK_NULL_HANDLE;
		VmaAllocator m_Allocator = VK_NULL_HANDLE;
		VmaAllocation m_Allocation = VK_NULL_HANDLE;
		bool m_Borrowed = false;
	};

	class VulkanBuffer : public VulkanResource
	{
	public:
		struct CreateInfo
		{
			VmaAllocator m_Allocator = VK_NULL_HANDLE;
			VkDevice m_Device = VK_NULL_HANDLE;
			VkBufferCreateInfo m_CreateInfo{};
			VmaAllocationCreateInfo m_AllocationCreateInfo{};
			RHIResourceState m_InitialState{};
			RHIMemoryUsage m_MemoryUsage = RHIMemoryUsage::GpuOnly;
		};

	public:
		VulkanBuffer() noexcept = default;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(VulkanBuffer);
		~VulkanBuffer() override = default;

		void Create(const CreateInfo& createInfo) noexcept;
		void AdoptExternal(
			VkDevice device, VkBuffer buffer, RHIResourceState initialState) noexcept;
		void Release() noexcept override;
		void SetDebugName(const char* name) noexcept override;

		[[nodiscard]] bool IsValid() const noexcept override { return m_Buffer != VK_NULL_HANDLE; }

		[[nodiscard]] VkBuffer Get() const noexcept { return m_Buffer; }
		[[nodiscard]] const VkBufferCreateInfo& GetCreateInfo() const noexcept
		{
			return m_CreateInfo;
		}
		[[nodiscard]] VkDeviceSize GetSizeInBytes() const noexcept { return m_CreateInfo.size; }
		[[nodiscard]] RHIResourceState GetInitialState() const noexcept { return m_InitialState; }
		[[nodiscard]] RHIMemoryUsage GetMemoryUsage() const noexcept { return m_MemoryUsage; }

		// Persistent mapping state; nullptr means unmapped.
		[[nodiscard]] void* GetMappedData() const noexcept { return m_MappedData; }
		void SetMappedData(void* data) noexcept { m_MappedData = data; }

	private:
		VkBuffer m_Buffer = VK_NULL_HANDLE;
		VkBufferCreateInfo m_CreateInfo{};
		RHIResourceState m_InitialState{};
		RHIMemoryUsage m_MemoryUsage = RHIMemoryUsage::GpuOnly;
		void* m_MappedData = nullptr;
	};

	class VulkanTexture : public VulkanResource
	{
	public:
		struct CreateInfo
		{
			VmaAllocator m_Allocator = VK_NULL_HANDLE;
			VkDevice m_Device = VK_NULL_HANDLE;
			VkImageCreateInfo m_CreateInfo{};
			VmaAllocationCreateInfo m_AllocationCreateInfo{};
			RHIResourceState m_InitialState{};
		};

	public:
		VulkanTexture() noexcept = default;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(VulkanTexture);
		~VulkanTexture() override = default;

		void Create(const CreateInfo& createInfo) noexcept;
		void AdoptExternal(
			VkDevice device, VkImage image, RHIResourceState initialState) noexcept;
		void Release() noexcept override;
		void SetDebugName(const char* name) noexcept override;

		[[nodiscard]] bool IsValid() const noexcept override { return m_Image != VK_NULL_HANDLE; }

		[[nodiscard]] VkImage Get() const noexcept { return m_Image; }
		[[nodiscard]] const VkImageCreateInfo& GetCreateInfo() const noexcept
		{
			return m_CreateInfo;
		}
		[[nodiscard]] VkFormat GetVkFormat() const noexcept { return m_CreateInfo.format; }
		[[nodiscard]] RHIResourceState GetInitialState() const noexcept { return m_InitialState; }

	private:
		VkImage m_Image = VK_NULL_HANDLE;
		VkImageCreateInfo m_CreateInfo{};
		RHIResourceState m_InitialState{};
	};
}
