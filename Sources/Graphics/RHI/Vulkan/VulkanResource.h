#pragma once
#include "Graphics/RHI/RHIBuffer.h"
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
	//
	// Ownership is strict RAII: the destructor releases the native handle
	// and, when owned, the VMA allocation. Copying and moving are deleted;
	// resources are managed exclusively through unique_ptr slots.
	class VulkanResource
	{
	public:
		VulkanResource() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanResource);
		virtual ~VulkanResource() = default;

		[[nodiscard]] virtual bool IsValid() const noexcept = 0;
		[[nodiscard]] bool IsExternal() const noexcept { return m_Borrowed; }
		[[nodiscard]] bool OwnsAllocation() const noexcept { return !m_Borrowed; }
		[[nodiscard]] VmaAllocation GetAllocation() const noexcept { return m_Allocation; }

		// Applies the debug name through VK_EXT_debug_utils when available.
		virtual void SetDebugName(const char* name) noexcept = 0;
		// Releases the native handle and, when owned, the VMA allocation.
		// Idempotent; the destructor calls it as well.
		virtual void Release() noexcept = 0;

	protected:
		VkDevice m_Device = VK_NULL_HANDLE;
		VmaAllocator m_Allocator = VK_NULL_HANDLE;
		VmaAllocation m_Allocation = VK_NULL_HANDLE;
		bool m_Borrowed = false;
	};

	// Who owns the unmap obligation of the current buffer mapping.
	// Persistent mappings are established by VMA at allocation time
	// (VMA_ALLOCATION_CREATE_MAPPED_BIT); their teardown is handled by
	// vmaDestroyBuffer and they must never be passed to vmaUnmapMemory.
	// Explicit mappings come from vmaMapMemory and must be paired with
	// vmaUnmapMemory before the allocation is released.
	enum class VulkanBufferMapping : uint8_t
	{
		None,
		Persistent,
		Explicit,
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
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanBuffer);
		~VulkanBuffer() override { Release(); }

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

		// Maps the buffer and returns a host pointer, or nullptr when the
		// buffer cannot be mapped (borrowed resources have no allocation
		// metadata and are never mapped by the backend). Persistent
		// mappings return the VMA-owned pointer directly; explicit
		// mappings are created here and must be released through Unmap.
		[[nodiscard]] void* Map(RHIMappedBufferRange readRange) noexcept;
		void Unmap(RHIMappedBufferRange writtenRange) noexcept;
		[[nodiscard]] VulkanBufferMapping GetMapping() const noexcept { return m_Mapping; }

	private:
		VkBuffer m_Buffer = VK_NULL_HANDLE;
		VkBufferCreateInfo m_CreateInfo{};
		RHIResourceState m_InitialState{};
		RHIMemoryUsage m_MemoryUsage = RHIMemoryUsage::GpuOnly;
		VulkanBufferMapping m_Mapping = VulkanBufferMapping::None;
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
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanTexture);
		~VulkanTexture() override { Release(); }

		void Create(const CreateInfo& createInfo) noexcept;
		void AdoptExternal(
			VkDevice device, VkImage image, RHIResourceState initialState) noexcept;
		void Release() noexcept override;
		void SetDebugName(const char* name) noexcept override;

		[[nodiscard]] bool IsValid() const noexcept override { return m_Image != VK_NULL_HANDLE; }

		[[nodiscard]] VkImage Get() const noexcept { return m_Image; }
		// Native creation history; owned resources only. Imported
		// resources keep this empty and rely on the RHI description held
		// by the resource manager instead.
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
