#include "Graphics/RHI/Vulkan/VulkanParameterBindings.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanTimelineFence.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"

#include <algorithm>
#include <cstring>

namespace gglab
{
	bool VulkanDynamicUniformState::Initialize(const VulkanBindingLayoutPlan& plan) noexcept
	{
		if (!plan.IsValid())
		{
			return false;
		}
		for (ShadowSlot& slot : m_Slots)
		{
			slot = {};
		}
		for (uint32_t bindingIndex = 0; bindingIndex < plan.m_Set0BindingCount; ++bindingIndex)
		{
			const VulkanSet0BindingPlan& binding = plan.m_Set0Bindings[bindingIndex];
			if (!binding.m_IsInlineConstants)
			{
				continue;
			}
			ShadowSlot& slot = m_Slots[binding.m_LogicalParameterIndex];
			slot.m_Bytes.resize(binding.m_SizeInBytes);
			slot.m_DynamicOffsetSlot = binding.m_DynamicOffsetSlot;
		}
		return true;
	}

	bool VulkanDynamicUniformState::UpdateShadow(uint32_t logicalParameterIndex,
		std::span<const uint32_t> values, uint32_t destOffsetInWords) noexcept
	{
		if (logicalParameterIndex >= m_Slots.size())
		{
			return false;
		}
		ShadowSlot& slot = m_Slots[logicalParameterIndex];
		const size_t destOffsetInBytes = static_cast<size_t>(destOffsetInWords) * sizeof(uint32_t);
		const size_t sourceSizeInBytes = values.size_bytes();
		if (slot.m_DynamicOffsetSlot == UINT32_MAX ||
			!IsValidInlineConstantWrite(static_cast<uint32_t>(slot.m_Bytes.size()),
				values.size(), destOffsetInWords))
		{
			return false;
		}
		std::memcpy(slot.m_Bytes.data() + destOffsetInBytes, values.data(), sourceSizeInBytes);
		return true;
	}

	std::span<const std::byte> VulkanDynamicUniformState::GetShadow(
		uint32_t logicalParameterIndex) const noexcept
	{
		return logicalParameterIndex < m_Slots.size() ?
			std::span<const std::byte>(m_Slots[logicalParameterIndex].m_Bytes) :
			std::span<const std::byte>{};
	}

	VulkanSet0DynamicUniformFrames::~VulkanSet0DynamicUniformFrames() noexcept
	{
		Finalize();
	}

	VkDescriptorPool VulkanSet0DynamicUniformFrames::CreatePool() noexcept
	{
		constexpr uint32_t SetsPerPool = 256;
		constexpr uint32_t DescriptorsPerPool = SetsPerPool * RHIBindingLayoutDesc::MaxSlots;
		const std::array sizes
		{
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, DescriptorsPerPool },
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DescriptorsPerPool },
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DescriptorsPerPool },
		};
		const VkDescriptorPoolCreateInfo info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = SetsPerPool,
			.poolSizeCount = static_cast<uint32_t>(sizes.size()),
			.pPoolSizes = sizes.data(),
		};
		VkDescriptorPool pool = VK_NULL_HANDLE;
		const VkResult result = vkCreateDescriptorPool(m_Device->Get(), &info, nullptr, &pool);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR("vkCreateDescriptorPool(set 0) failed with {}.", ToString(result));
			return VK_NULL_HANDLE;
		}
		return pool;
	}

	bool VulkanSet0DynamicUniformFrames::Initialize(
		VulkanDevice* device, uint32_t frameSlotCount) noexcept
	{
		if (m_Device || !device || frameSlotCount == 0 ||
			!device->RequireOwnerThread("VulkanSet0DynamicUniformFrames::Initialize"))
		{
			return false;
		}
		m_Device = device;
		m_FrameSlots.resize(frameSlotCount);
		return true;
	}

	void VulkanSet0DynamicUniformFrames::Finalize() noexcept
	{
		if (!m_Device)
		{
			return;
		}
		if (!m_Device->RequireOwnerThread("VulkanSet0DynamicUniformFrames::Finalize"))
		{
			return;
		}
		for (const auto& slot : m_FrameSlots)
		{
			GGLAB_ASSERT_MSG(!slot.m_Active,
				"Discard or submit descriptor command streams before finalization.");
			if (slot.m_LastSubmittedFence.IsValid() &&
				!m_Device->IsFencePointCompleted(slot.m_LastSubmittedFence))
			{
				const VkResult result = vkQueueWaitIdle(m_Device->GetGraphicsQueue());
				if (result != VK_SUCCESS)
				{
					GGLAB_LOG_GRAPHICS_ERROR(
						"Waiting to destroy Vulkan descriptor pools failed with {}.", ToString(result));
				}
				break;
			}
		}
		for (const auto& slot : m_FrameSlots)
		{
			for (const auto pool : slot.m_Pools)
			{
				vkDestroyDescriptorPool(m_Device->Get(), pool, nullptr);
			}
		}
		m_FrameSlots.clear();
		m_Device = nullptr;
	}

	bool VulkanSet0DynamicUniformFrames::BeginFrame(uint32_t frameSlotIndex) noexcept
	{
		if (!m_Device || frameSlotIndex >= m_FrameSlots.size() ||
			!m_Device->RequireOwnerThread("VulkanSet0DynamicUniformFrames::BeginFrame"))
		{
			return false;
		}
		auto& slot = m_FrameSlots[frameSlotIndex];
		if (slot.m_Active || (slot.m_LastSubmittedFence.IsValid() &&
			!m_Device->IsFencePointCompleted(slot.m_LastSubmittedFence)))
		{
			return false;
		}
		for (const auto pool : slot.m_Pools)
		{
			const VkResult result = vkResetDescriptorPool(m_Device->Get(), pool, 0);
			if (result != VK_SUCCESS)
			{
				GGLAB_LOG_GRAPHICS_ERROR("vkResetDescriptorPool(set 0) failed with {}.", ToString(result));
				return false;
			}
		}
		slot.m_PoolIndex = 0;
		slot.m_AllocationCount = 0;
		slot.m_LastSubmittedFence = {};
		slot.m_Active = true;
		return true;
	}

	bool VulkanSet0DynamicUniformFrames::EndFrame(
		uint32_t frameSlotIndex, const RHIFencePoint& fence) noexcept
	{
		const auto* timeline = m_Device ? m_Device->GetGraphicsTimeline() : nullptr;
		if (!timeline || frameSlotIndex >= m_FrameSlots.size() || !m_FrameSlots[frameSlotIndex].m_Active ||
			!fence.IsValid() || fence.m_Fence != timeline->GetRHIHandle() ||
			fence.m_Value > timeline->GetCurrentSignalValue())
		{
			return false;
		}
		auto& slot = m_FrameSlots[frameSlotIndex];
		slot.m_LastSubmittedFence = fence;
		slot.m_Active = false;
		return true;
	}

	bool VulkanSet0DynamicUniformFrames::RetainFrame(
		uint32_t frameSlotIndex, const RHIFencePoint& reservedFence) noexcept
	{
		const auto* timeline = m_Device ? m_Device->GetGraphicsTimeline() : nullptr;
		if (!timeline || frameSlotIndex >= m_FrameSlots.size() || !m_FrameSlots[frameSlotIndex].m_Active ||
			!reservedFence.IsValid() || reservedFence.m_Fence != timeline->GetRHIHandle())
		{
			return false;
		}
		auto& slot = m_FrameSlots[frameSlotIndex];
		slot.m_LastSubmittedFence = reservedFence;
		slot.m_Active = false;
		return true;
	}

	bool VulkanSet0DynamicUniformFrames::AbortFrame(uint32_t frameSlotIndex) noexcept
	{
		if (!m_Device || frameSlotIndex >= m_FrameSlots.size() || !m_FrameSlots[frameSlotIndex].m_Active)
		{
			return false;
		}
		m_FrameSlots[frameSlotIndex].m_Active = false;
		return true;
	}

	bool VulkanSet0DynamicUniformFrames::IsFrameActive(uint32_t frameSlotIndex) const noexcept
	{
		return frameSlotIndex < m_FrameSlots.size() && m_FrameSlots[frameSlotIndex].m_Active;
	}

	VkDescriptorSet VulkanSet0DynamicUniformFrames::AllocateDescriptorSet(uint32_t frameSlotIndex,
		const VulkanBindingLayout& bindingLayout,
		std::span<const VulkanSet0BufferBinding> bufferBindings) noexcept
	{
		if (m_Device == nullptr || !bindingLayout.IsValid() ||
			frameSlotIndex >= m_FrameSlots.size() || !m_FrameSlots[frameSlotIndex].m_Active ||
			!m_Device->RequireOwnerThread("VulkanSet0DynamicUniformFrames::AllocateDescriptorSet"))
		{
			return VK_NULL_HANDLE;
		}

		FrameSlot& slot = m_FrameSlots[frameSlotIndex];
		const VkDescriptorSetLayout layout = bindingLayout.GetSet0Layout();
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		// Grow in bounded chunks; existing immutable snapshots remain untouched.
		for (;;)
		{
			if (slot.m_PoolIndex == slot.m_Pools.size())
			{
				constexpr size_t MaxPoolsPerFrame = 64;
				const auto pool = slot.m_Pools.size() < MaxPoolsPerFrame ? CreatePool() : VK_NULL_HANDLE;
				if (pool == VK_NULL_HANDLE)
				{
					GGLAB_LOG_GRAPHICS_ERROR(
						"Vulkan descriptor snapshot pool growth failed or exceeded its budget.");
					return VK_NULL_HANDLE;
				}
				slot.m_Pools.push_back(pool);
			}
			const VkDescriptorSetAllocateInfo info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = slot.m_Pools[slot.m_PoolIndex],
				.descriptorSetCount = 1,
				.pSetLayouts = &layout,
			};
			const auto result = vkAllocateDescriptorSets(m_Device->Get(), &info, &descriptorSet);
			if (result == VK_SUCCESS)
			{
				break;
			}
			if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
			{
				GGLAB_LOG_GRAPHICS_ERROR("vkAllocateDescriptorSets(set 0) failed with {}.", ToString(result));
				return VK_NULL_HANDLE;
			}
			++slot.m_PoolIndex;
		}

		const VulkanBindingLayoutPlan& plan = bindingLayout.GetPlan();
		std::vector<VkDescriptorBufferInfo> bufferInfos;
		std::vector<VkWriteDescriptorSet> writes;
		bufferInfos.reserve(bufferBindings.size());
		writes.reserve(bufferBindings.size());
		for (const VulkanSet0BufferBinding& source : bufferBindings)
		{
			const auto bindings = std::span<const VulkanSet0BindingPlan>(
				plan.m_Set0Bindings.data(), plan.m_Set0BindingCount);
			const auto binding = std::ranges::find_if(bindings,
				[&source](const VulkanSet0BindingPlan& candidate) noexcept
				{
					return candidate.m_LogicalParameterIndex == source.m_LogicalParameterIndex;
				});
			if (binding == bindings.end() ||
				binding->m_DescriptorType != source.m_DescriptorType ||
				source.m_Buffer == VK_NULL_HANDLE || source.m_Range == 0)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"Vulkan set-0 snapshot rejected an invalid fixed-buffer binding.");
				return VK_NULL_HANDLE;
			}
			bufferInfos.push_back({
				.buffer = source.m_Buffer,
				.offset = source.m_Offset,
				.range = source.m_Range,
				});
			writes.push_back({
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptorSet,
				.dstBinding = binding->m_Binding,
				.descriptorCount = 1,
				.descriptorType = binding->m_DescriptorType,
				.pBufferInfo = &bufferInfos.back(),
				});
		}
		if (!writes.empty())
		{
			vkUpdateDescriptorSets(m_Device->Get(),
				static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}
		++slot.m_AllocationCount;
		return descriptorSet;
	}
}
