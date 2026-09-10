#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gglab
{
	class VulkanDevice;

	struct VulkanDynamicUniformArenaConfig
	{
		uint32_t m_PageSizeInBytes = 64 * 1024;
		uint32_t m_MaxPageCount = 4;
		uint32_t m_Alignment = 1;
	};

	struct VulkanDynamicUniformArenaAllocation
	{
		uint32_t m_OffsetInBytes = 0;
		uint32_t m_SizeInBytes = 0;
		uint32_t m_PageIndex = 0;

		[[nodiscard]] bool IsValid() const noexcept { return m_SizeInBytes != 0; }
	};

	struct VulkanDynamicUniformDiagnostics
	{
		uint32_t m_BytesAllocated = 0;
		uint32_t m_PageCount = 0;
		uint32_t m_HighWaterMarkInBytes = 0;
		uint64_t m_OverflowCount = 0;
		uint64_t m_FrameFailureCount = 0;
	};

	class VulkanDynamicUniformArena
	{
	public:
		explicit VulkanDynamicUniformArena(
			const VulkanDynamicUniformArenaConfig& config = {}) noexcept;

		[[nodiscard]] VulkanDynamicUniformArenaAllocation Allocate(uint32_t sizeInBytes) noexcept;
		void Reset() noexcept;
		void RecordFrameFailure() noexcept { ++m_Diagnostics.m_FrameFailureCount; }

		[[nodiscard]] uint32_t GetCapacityInBytes() const noexcept;
		[[nodiscard]] const VulkanDynamicUniformDiagnostics& GetDiagnostics() const noexcept
		{
			return m_Diagnostics;
		}

	private:
		VulkanDynamicUniformArenaConfig m_Config{};
		uint32_t m_Cursor = 0;
		VulkanDynamicUniformDiagnostics m_Diagnostics{};
	};

	struct VulkanDynamicUniformAllocation
	{
		RHIBufferHandle m_Buffer{};
		VkBuffer m_NativeBuffer = VK_NULL_HANDLE;
		uint32_t m_DynamicOffset = 0;
		uint32_t m_SizeInBytes = 0;
		uint32_t m_PageIndex = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Buffer.IsValid() && m_NativeBuffer != VK_NULL_HANDLE && m_SizeInBytes != 0;
		}
	};

	class VulkanDynamicUniformBuffer
	{
	public:
		VulkanDynamicUniformBuffer() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanDynamicUniformBuffer);
		~VulkanDynamicUniformBuffer() noexcept;

		[[nodiscard]] bool Initialize(VulkanDevice* device, uint32_t frameSlotCount,
			const VulkanDynamicUniformArenaConfig& config = {}) noexcept;
		void Finalize() noexcept;
		[[nodiscard]] bool BeginFrame(uint32_t frameSlotIndex) noexcept;
		[[nodiscard]] bool EndFrame(
			uint32_t frameSlotIndex, const RHIFencePoint& submittedFence) noexcept;
		[[nodiscard]] bool AbortFrame(uint32_t frameSlotIndex) noexcept;
		[[nodiscard]] VulkanDynamicUniformAllocation Write(
			uint32_t frameSlotIndex, std::span<const std::byte> payload) noexcept;

		[[nodiscard]] VkBuffer GetNativeBuffer(uint32_t frameSlotIndex) const noexcept;
		[[nodiscard]] bool IsFrameActive(uint32_t frameSlotIndex) const noexcept;
		[[nodiscard]] uint64_t GetFrameGeneration(uint32_t frameSlotIndex) const noexcept;
		[[nodiscard]] uint32_t GetCapacityInBytes() const noexcept { return m_CapacityInBytes; }
		[[nodiscard]] uint32_t GetAlignmentInBytes() const noexcept { return m_AlignmentInBytes; }
		[[nodiscard]] const VulkanDynamicUniformDiagnostics* GetDiagnostics(
			uint32_t frameSlotIndex) const noexcept;

	private:
		struct FrameSlot
		{
			std::unique_ptr<VulkanDynamicUniformArena> m_Arena;
			RHIBufferHandle m_Buffer{};
			std::byte* m_MappedData = nullptr;
			RHIFencePoint m_LastSubmittedFence{};
			uint64_t m_FrameGeneration = 0;
			bool m_Active = false;
		};

		VulkanDevice* m_Device = nullptr;
		std::vector<FrameSlot> m_FrameSlots;
		uint32_t m_CapacityInBytes = 0;
		uint32_t m_AlignmentInBytes = 0;
	};

	struct VulkanDynamicUniformUpdate
	{
		VulkanDynamicUniformAllocation m_Allocation{};
		uint32_t m_DynamicOffsetSlot = UINT32_MAX;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Allocation.IsValid() && m_DynamicOffsetSlot != UINT32_MAX;
		}
	};

	struct VulkanSet0BufferBinding
	{
		uint32_t m_LogicalParameterIndex = UINT32_MAX;
		VkDescriptorType m_DescriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
		VkBuffer m_Buffer = VK_NULL_HANDLE;
		VkDeviceSize m_Offset = 0;
		VkDeviceSize m_Range = 0;

		bool operator==(const VulkanSet0BufferBinding&) const noexcept = default;
	};

	class VulkanDynamicUniformState
	{
	public:
		[[nodiscard]] bool Initialize(const VulkanBindingLayoutPlan& plan) noexcept;
		[[nodiscard]] bool UpdateShadow(uint32_t logicalParameterIndex,
			std::span<const uint32_t> values, uint32_t destOffsetInWords) noexcept;
		[[nodiscard]] VulkanDynamicUniformUpdate SetPushConstants(uint32_t logicalParameterIndex,
			std::span<const uint32_t> values, uint32_t destOffsetInWords,
			VulkanDynamicUniformBuffer& buffer, uint32_t frameSlotIndex) noexcept;

		[[nodiscard]] std::span<const std::byte> GetShadow(
			uint32_t logicalParameterIndex) const noexcept;

	private:
		struct ShadowSlot
		{
			std::vector<std::byte> m_Bytes;
			uint32_t m_DynamicOffsetSlot = UINT32_MAX;
		};

		std::array<ShadowSlot, RHIBindingLayoutDesc::MaxSlots> m_Slots{};
	};

	// Owns frame-local descriptor pools and immutable set-0 snapshots. A frame
	// arena serves every binding layout used by that command stream; each
	// snapshot contains the layout's dynamic-uniform bindings plus the fixed
	// buffer bindings supplied by the command context. Pool reset remains
	// fence-gated.
	class VulkanSet0DynamicUniformFrames
	{
	public:
		VulkanSet0DynamicUniformFrames() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanSet0DynamicUniformFrames);
		~VulkanSet0DynamicUniformFrames() noexcept;

		[[nodiscard]] bool Initialize(VulkanDevice* device,
			VulkanDynamicUniformBuffer* uniformBuffer, uint32_t frameSlotCount) noexcept;
		void Finalize() noexcept;
		[[nodiscard]] bool BeginFrame(uint32_t frameSlotIndex) noexcept;
		[[nodiscard]] bool EndFrame(
			uint32_t frameSlotIndex, const RHIFencePoint& submittedFence) noexcept;
		[[nodiscard]] bool AbortFrame(uint32_t frameSlotIndex) noexcept;
		[[nodiscard]] bool IsFrameActive(uint32_t frameSlotIndex) const noexcept;
		[[nodiscard]] VkDescriptorSet AllocateDescriptorSet(uint32_t frameSlotIndex,
			const VulkanBindingLayout& layout,
			std::span<const VulkanSet0BufferBinding> bufferBindings) noexcept;

	private:
		struct FrameSlot
		{
			VkDescriptorPool m_Pool = VK_NULL_HANDLE;
			uint64_t m_LastResetGeneration = 0;
			RHIFencePoint m_LastSubmittedFence{};
			bool m_Active = false;
		};

		VulkanDevice* m_Device = nullptr;
		VulkanDynamicUniformBuffer* m_UniformBuffer = nullptr;
		std::vector<FrameSlot> m_FrameSlots;
	};
}
