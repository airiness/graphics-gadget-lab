#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gglab
{
	class VulkanDevice;

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

	// Backend-owned immutable descriptor snapshots. Constant allocation and host
	// write publication belong to RHIConstantUploadBatch; descriptor reset is
	// independently gated by the submitted command stream's fence.
	class VulkanSet0DynamicUniformFrames
	{
	public:
		VulkanSet0DynamicUniformFrames() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanSet0DynamicUniformFrames);
		~VulkanSet0DynamicUniformFrames() noexcept;

		[[nodiscard]] bool Initialize(VulkanDevice* device,
			uint32_t frameSlotCount) noexcept;
		void Finalize() noexcept;
		[[nodiscard]] bool BeginFrame(uint32_t frameSlotIndex) noexcept;
		[[nodiscard]] bool EndFrame(
			uint32_t frameSlotIndex, const RHIFencePoint& submittedFence) noexcept;
		// Closes a frame whose accepted submission has no committed signal. The arena stays
		// retained on the reserved graphics timeline point until teardown.
		[[nodiscard]] bool RetainFrame(
			uint32_t frameSlotIndex, const RHIFencePoint& reservedFence) noexcept;
		[[nodiscard]] bool AbortFrame(uint32_t frameSlotIndex) noexcept;
		[[nodiscard]] bool IsFrameActive(uint32_t frameSlotIndex) const noexcept;
		[[nodiscard]] VkDescriptorSet AllocateDescriptorSet(uint32_t frameSlotIndex,
			const VulkanBindingLayout& layout,
			std::span<const VulkanSet0BufferBinding> bufferBindings) noexcept;

	private:
		struct FrameSlot
		{
			std::vector<VkDescriptorPool> m_Pools;
			size_t m_PoolIndex = 0;
			RHIFencePoint m_LastSubmittedFence{};
			bool m_Active = false;
		};

		[[nodiscard]] VkDescriptorPool CreatePool() noexcept;
		VulkanDevice* m_Device = nullptr;
		std::vector<FrameSlot> m_FrameSlots;
	};
}
