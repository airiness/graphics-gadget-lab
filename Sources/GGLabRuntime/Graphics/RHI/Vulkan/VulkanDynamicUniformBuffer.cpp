#include "Graphics/RHI/Vulkan/VulkanDynamicUniformBuffer.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanResource.h"
#include "Graphics/RHI/Vulkan/VulkanResourceManager.h"
#include "Graphics/RHI/Vulkan/VulkanTimelineFence.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"
#include "GGLabFoundation/Base/MathUtils.h"

#include <algorithm>
#include <cstring>
#include <numeric>

namespace gglab
{
	namespace
	{
		constexpr uint32_t Set0DescriptorSetsPerFrame = 256;
	}

	VulkanDynamicUniformArena::VulkanDynamicUniformArena(
		const VulkanDynamicUniformArenaConfig& config) noexcept : m_Config(config)
	{
		m_Config.m_PageSizeInBytes = std::max(m_Config.m_PageSizeInBytes, 1u);
		m_Config.m_MaxPageCount = std::max(m_Config.m_MaxPageCount, 1u);
		m_Config.m_Alignment = std::max(m_Config.m_Alignment, 1u);
		const uint64_t alignedPageSize = utils::AlignUp<uint64_t>(
			m_Config.m_PageSizeInBytes, m_Config.m_Alignment);
		m_Config.m_PageSizeInBytes = alignedPageSize <= UINT32_MAX
			? static_cast<uint32_t>(alignedPageSize)
			: 0;
	}

	VulkanDynamicUniformArenaAllocation VulkanDynamicUniformArena::Allocate(
		uint32_t sizeInBytes) noexcept
	{
		if (sizeInBytes == 0 || sizeInBytes > m_Config.m_PageSizeInBytes)
		{
			++m_Diagnostics.m_OverflowCount;
			return {};
		}
		const uint32_t capacity = GetCapacityInBytes();
		uint64_t offset = utils::AlignUp<uint64_t>(m_Cursor, m_Config.m_Alignment);
		uint64_t pageIndex = offset / m_Config.m_PageSizeInBytes;
		const uint64_t pageEnd = (pageIndex + 1) * m_Config.m_PageSizeInBytes;
		if (offset + sizeInBytes > pageEnd)
		{
			++pageIndex;
			offset = pageIndex * static_cast<uint64_t>(m_Config.m_PageSizeInBytes);
		}
		if (capacity == 0 || pageIndex >= m_Config.m_MaxPageCount ||
			sizeInBytes > capacity || offset > capacity - sizeInBytes)
		{
			++m_Diagnostics.m_OverflowCount;
			return {};
		}

		m_Cursor = static_cast<uint32_t>(offset + sizeInBytes);
		m_Diagnostics.m_BytesAllocated = m_Cursor;
		m_Diagnostics.m_PageCount = static_cast<uint32_t>(pageIndex + 1);
		m_Diagnostics.m_HighWaterMarkInBytes =
			std::max(m_Diagnostics.m_HighWaterMarkInBytes, m_Cursor);
		return {
			.m_OffsetInBytes = static_cast<uint32_t>(offset),
			.m_SizeInBytes = sizeInBytes,
			.m_PageIndex = static_cast<uint32_t>(pageIndex),
		};
	}

	void VulkanDynamicUniformArena::Reset() noexcept
	{
		m_Cursor = 0;
		m_Diagnostics.m_BytesAllocated = 0;
		m_Diagnostics.m_PageCount = 0;
	}

	uint32_t VulkanDynamicUniformArena::GetCapacityInBytes() const noexcept
	{
		const uint64_t capacity = static_cast<uint64_t>(m_Config.m_PageSizeInBytes) *
			m_Config.m_MaxPageCount;
		return capacity <= UINT32_MAX ? static_cast<uint32_t>(capacity) : 0;
	}

	VulkanDynamicUniformBuffer::~VulkanDynamicUniformBuffer() noexcept
	{
		Finalize();
	}

	bool VulkanDynamicUniformBuffer::Initialize(VulkanDevice* device, uint32_t frameSlotCount,
		const VulkanDynamicUniformArenaConfig& config) noexcept
	{
		if (m_Device != nullptr || device == nullptr || frameSlotCount == 0 ||
			!device->RequireOwnerThread("VulkanDynamicUniformBuffer::Initialize"))
		{
			return false;
		}
		VulkanDynamicUniformArenaConfig effectiveConfig = config;
		const uint64_t requestedAlignment = std::max<uint64_t>(config.m_Alignment, 1);
		const uint64_t deviceAlignment = std::max<VkDeviceSize>(
			device->GetPhysicalDeviceLimits().minUniformBufferOffsetAlignment, 1);
		const uint64_t alignmentFactor = requestedAlignment /
			std::gcd(requestedAlignment, deviceAlignment);
		if (deviceAlignment > UINT32_MAX / alignmentFactor)
		{
			return false;
		}
		const uint64_t alignment = alignmentFactor * deviceAlignment;
		effectiveConfig.m_Alignment = static_cast<uint32_t>(alignment);
		VulkanDynamicUniformArena capacityProbe(effectiveConfig);
		m_CapacityInBytes = capacityProbe.GetCapacityInBytes();
		if (m_CapacityInBytes == 0)
		{
			return false;
		}

		m_Device = device;
		m_AlignmentInBytes = effectiveConfig.m_Alignment;
		m_FrameSlots.resize(frameSlotCount);
		for (uint32_t frameSlotIndex = 0; frameSlotIndex < frameSlotCount; ++frameSlotIndex)
		{
			FrameSlot& slot = m_FrameSlots[frameSlotIndex];
			slot.m_Arena = std::make_unique<VulkanDynamicUniformArena>(effectiveConfig);
			slot.m_Buffer = device->CreateBuffer({
				.m_SizeInBytes = m_CapacityInBytes,
				.m_Usage = RHIBufferUsage::Constant,
				.m_MemoryUsage = RHIMemoryUsage::CpuToGpu,
				}, {
					.m_Domain = RHIResourceDebugDomain::Renderer,
					.m_Label = "Vulkan.DynamicUniformBuffer",
				});
				if (!slot.m_Buffer.IsValid())
				{
					Finalize();
					return false;
				}
				slot.m_MappedData = static_cast<std::byte*>(device->MapBuffer(
					slot.m_Buffer, { .m_Begin = 0, .m_End = 0 }));
				if (slot.m_MappedData == nullptr)
				{
					Finalize();
					return false;
				}
		}
		return true;
	}

	void VulkanDynamicUniformBuffer::Finalize() noexcept
	{
		if (m_Device == nullptr)
		{
			return;
		}
		if (!m_Device->RequireOwnerThread("VulkanDynamicUniformBuffer::Finalize"))
		{
			return;
		}
		for (FrameSlot& slot : m_FrameSlots)
		{
			if (slot.m_Buffer.IsValid())
			{
				m_Device->DestroyBuffer(slot.m_Buffer);
			}
			slot = {};
		}
		m_FrameSlots.clear();
		m_CapacityInBytes = 0;
		m_AlignmentInBytes = 0;
		m_Device = nullptr;
	}

	bool VulkanDynamicUniformBuffer::BeginFrame(uint32_t frameSlotIndex) noexcept
	{
		if (m_Device == nullptr ||
			!m_Device->RequireOwnerThread("VulkanDynamicUniformBuffer::BeginFrame") ||
			frameSlotIndex >= m_FrameSlots.size())
		{
			return false;
		}
		FrameSlot& slot = m_FrameSlots[frameSlotIndex];
		if (slot.m_Active || (slot.m_LastSubmittedFence.IsValid() &&
			!m_Device->IsFencePointCompleted(slot.m_LastSubmittedFence)))
		{
			slot.m_Arena->RecordFrameFailure();
			return false;
		}
		slot.m_Arena->Reset();
		slot.m_LastSubmittedFence = {};
		++slot.m_FrameGeneration;
		if (slot.m_FrameGeneration == 0)
		{
			slot.m_FrameGeneration = 1;
		}
		slot.m_Active = true;
		return true;
	}

	bool VulkanDynamicUniformBuffer::EndFrame(
		uint32_t frameSlotIndex, const RHIFencePoint& submittedFence) noexcept
	{
		if (m_Device == nullptr ||
			!m_Device->RequireOwnerThread("VulkanDynamicUniformBuffer::EndFrame") ||
			frameSlotIndex >= m_FrameSlots.size())
		{
			return false;
		}
		const VulkanTimelineFence* timeline = m_Device->GetGraphicsTimeline();
		if (timeline == nullptr || submittedFence.m_Fence != timeline->GetRHIHandle() ||
			submittedFence.m_Value > timeline->GetCurrentSignalValue())
		{
			return false;
		}
		FrameSlot& slot = m_FrameSlots[frameSlotIndex];
		if (!slot.m_Active)
		{
			return false;
		}
		m_Device->RecordBufferUse(slot.m_Buffer, submittedFence);
		slot.m_LastSubmittedFence = submittedFence;
		slot.m_Active = false;
		return true;
	}

	bool VulkanDynamicUniformBuffer::AbortFrame(uint32_t frameSlotIndex) noexcept
	{
		if (m_Device == nullptr ||
			!m_Device->RequireOwnerThread("VulkanDynamicUniformBuffer::AbortFrame") ||
			frameSlotIndex >= m_FrameSlots.size() || !m_FrameSlots[frameSlotIndex].m_Active)
		{
			return false;
		}
		m_FrameSlots[frameSlotIndex].m_Active = false;
		return true;
	}

	VulkanDynamicUniformAllocation VulkanDynamicUniformBuffer::Write(
		uint32_t frameSlotIndex, std::span<const std::byte> payload) noexcept
	{
		if (m_Device == nullptr ||
			!m_Device->RequireOwnerThread("VulkanDynamicUniformBuffer::Write") ||
			frameSlotIndex >= m_FrameSlots.size() || payload.empty() ||
			payload.size() > m_Device->GetPhysicalDeviceLimits().maxUniformBufferRange)
		{
			return {};
		}
		FrameSlot& slot = m_FrameSlots[frameSlotIndex];
		if (!slot.m_Active)
		{
			slot.m_Arena->RecordFrameFailure();
			return {};
		}
		const VulkanDynamicUniformArenaAllocation allocation =
			slot.m_Arena->Allocate(static_cast<uint32_t>(payload.size()));
		if (!allocation.IsValid())
		{
			slot.m_Arena->RecordFrameFailure();
			return {};
		}
		std::memcpy(slot.m_MappedData + allocation.m_OffsetInBytes,
			payload.data(), payload.size());
		m_Device->UnmapBuffer(slot.m_Buffer, {
			.m_Begin = allocation.m_OffsetInBytes,
			.m_End = allocation.m_OffsetInBytes + allocation.m_SizeInBytes,
			});
		const VulkanBuffer* native = m_Device->GetResourceManager().ResolveBuffer(slot.m_Buffer);
		return native
			? VulkanDynamicUniformAllocation{
				.m_Buffer = slot.m_Buffer,
				.m_NativeBuffer = native->Get(),
				.m_DynamicOffset = allocation.m_OffsetInBytes,
				.m_SizeInBytes = allocation.m_SizeInBytes,
				.m_PageIndex = allocation.m_PageIndex,
		}
		: VulkanDynamicUniformAllocation{};
	}

	VkBuffer VulkanDynamicUniformBuffer::GetNativeBuffer(uint32_t frameSlotIndex) const noexcept
	{
		if (m_Device == nullptr || frameSlotIndex >= m_FrameSlots.size())
		{
			return VK_NULL_HANDLE;
		}
		const VulkanBuffer* native =
			m_Device->GetResourceManager().ResolveBuffer(m_FrameSlots[frameSlotIndex].m_Buffer);
		return native ? native->Get() : VK_NULL_HANDLE;
	}

	bool VulkanDynamicUniformBuffer::IsFrameActive(uint32_t frameSlotIndex) const noexcept
	{
		return frameSlotIndex < m_FrameSlots.size() && m_FrameSlots[frameSlotIndex].m_Active;
	}

	uint64_t VulkanDynamicUniformBuffer::GetFrameGeneration(uint32_t frameSlotIndex) const noexcept
	{
		return frameSlotIndex < m_FrameSlots.size()
			? m_FrameSlots[frameSlotIndex].m_FrameGeneration
			: 0;
	}

	const VulkanDynamicUniformDiagnostics* VulkanDynamicUniformBuffer::GetDiagnostics(
		uint32_t frameSlotIndex) const noexcept
	{
		return frameSlotIndex < m_FrameSlots.size()
			? &m_FrameSlots[frameSlotIndex].m_Arena->GetDiagnostics()
			: nullptr;
	}

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
			if (binding.m_DescriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
			{
				continue;
			}
			ShadowSlot& slot = m_Slots[binding.m_LogicalParameterIndex];
			slot.m_Bytes.resize(binding.m_SizeInBytes);
			slot.m_DynamicOffsetSlot = binding.m_DynamicOffsetSlot;
		}
		return true;
	}

	VulkanDynamicUniformUpdate VulkanDynamicUniformState::SetPushConstants(
		uint32_t logicalParameterIndex, std::span<const uint32_t> values,
		uint32_t destOffsetInWords, VulkanDynamicUniformBuffer& buffer,
		uint32_t frameSlotIndex) noexcept
	{
		if (!UpdateShadow(logicalParameterIndex, values, destOffsetInWords))
		{
			return {};
		}
		ShadowSlot& slot = m_Slots[logicalParameterIndex];
		return {
			.m_Allocation = buffer.Write(frameSlotIndex, slot.m_Bytes),
			.m_DynamicOffsetSlot = slot.m_DynamicOffsetSlot,
		};
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
		if (slot.m_DynamicOffsetSlot == UINT32_MAX || values.empty() ||
			destOffsetInBytes > slot.m_Bytes.size() ||
			sourceSizeInBytes > slot.m_Bytes.size() - destOffsetInBytes)
		{
			return false;
		}
		std::memcpy(slot.m_Bytes.data() + destOffsetInBytes, values.data(), sourceSizeInBytes);
		return true;
	}

	std::span<const std::byte> VulkanDynamicUniformState::GetShadow(
		uint32_t logicalParameterIndex) const noexcept
	{
		return logicalParameterIndex < m_Slots.size()
			? std::span<const std::byte>(m_Slots[logicalParameterIndex].m_Bytes)
			: std::span<const std::byte>{};
	}

	VulkanSet0DynamicUniformFrames::~VulkanSet0DynamicUniformFrames() noexcept
	{
		Finalize();
	}

	bool VulkanSet0DynamicUniformFrames::Initialize(VulkanDevice* device,
		const VulkanBindingLayout& layout, VulkanDynamicUniformBuffer* uniformBuffer,
		uint32_t frameSlotCount) noexcept
	{
		if (m_Device != nullptr || device == nullptr || !layout.IsValid() ||
			uniformBuffer == nullptr || frameSlotCount == 0 ||
			!device->RequireOwnerThread("VulkanSet0DynamicUniformFrames::Initialize"))
		{
			return false;
		}
		m_Device = device;
		m_Layout = &layout;
		m_UniformBuffer = uniformBuffer;
		m_FrameSlots.resize(frameSlotCount);

		std::vector<VkDescriptorPoolSize> poolSizes;
		for (uint32_t bindingIndex = 0;
			bindingIndex < layout.GetPlan().m_Set0BindingCount; ++bindingIndex)
		{
			const VulkanSet0BindingPlan& binding = layout.GetPlan().m_Set0Bindings[bindingIndex];
			auto iterator = std::ranges::find_if(poolSizes,
				[&binding](const VkDescriptorPoolSize& size) noexcept
				{
					return size.type == binding.m_DescriptorType;
				});
			if (iterator == poolSizes.end())
			{
				poolSizes.push_back({ binding.m_DescriptorType,
					binding.m_DescriptorCount * Set0DescriptorSetsPerFrame });
			}
			else
			{
				iterator->descriptorCount +=
					binding.m_DescriptorCount * Set0DescriptorSetsPerFrame;
			}
		}
		for (uint32_t frameSlotIndex = 0; frameSlotIndex < frameSlotCount; ++frameSlotIndex)
		{
			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.maxSets = Set0DescriptorSetsPerFrame;
			poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
			poolInfo.pPoolSizes = poolSizes.data();
			const VkResult result = vkCreateDescriptorPool(
				device->Get(), &poolInfo, nullptr, &m_FrameSlots[frameSlotIndex].m_Pool);
			if (result != VK_SUCCESS)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"vkCreateDescriptorPool(set 0) failed with {}.", ToString(result));
				Finalize();
				return false;
			}
		}
		return true;
	}

	void VulkanSet0DynamicUniformFrames::Finalize() noexcept
	{
		if (m_Device == nullptr)
		{
			return;
		}
		if (!m_Device->RequireOwnerThread("VulkanSet0DynamicUniformFrames::Finalize"))
		{
			return;
		}
		bool requiresWait = false;
		for (uint32_t frameSlotIndex = 0; frameSlotIndex < m_FrameSlots.size(); ++frameSlotIndex)
		{
			FrameSlot& slot = m_FrameSlots[frameSlotIndex];
			if (slot.m_Active)
			{
				(void)m_UniformBuffer->AbortFrame(frameSlotIndex);
				slot.m_Active = false;
			}
			requiresWait = requiresWait || (slot.m_LastSubmittedFence.IsValid() &&
				!m_Device->IsFencePointCompleted(slot.m_LastSubmittedFence));
		}
		if (requiresWait)
		{
			const VkResult result = vkQueueWaitIdle(m_Device->GetGraphicsQueue());
			if (result != VK_SUCCESS)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"Waiting to destroy Vulkan set 0 descriptor pools failed with {}.",
					ToString(result));
			}
		}
		for (FrameSlot& slot : m_FrameSlots)
		{
			if (slot.m_Pool != VK_NULL_HANDLE)
			{
				vkDestroyDescriptorPool(m_Device->Get(), slot.m_Pool, nullptr);
			}
		}
		m_FrameSlots.clear();
		m_UniformBuffer = nullptr;
		m_Layout = nullptr;
		m_Device = nullptr;
	}

	bool VulkanSet0DynamicUniformFrames::BeginFrame(uint32_t frameSlotIndex) noexcept
	{
		if (m_Device == nullptr || frameSlotIndex >= m_FrameSlots.size() ||
			!m_Device->RequireOwnerThread("VulkanSet0DynamicUniformFrames::BeginFrame"))
		{
			return false;
		}
		FrameSlot& slot = m_FrameSlots[frameSlotIndex];
		if (slot.m_Active || (slot.m_LastSubmittedFence.IsValid() &&
			!m_Device->IsFencePointCompleted(slot.m_LastSubmittedFence)) ||
			!m_UniformBuffer->BeginFrame(frameSlotIndex))
		{
			return false;
		}
		const uint64_t frameGeneration = m_UniformBuffer->GetFrameGeneration(frameSlotIndex);
		if (frameGeneration == 0 ||
			frameGeneration <= slot.m_LastResetGeneration)
		{
			(void)m_UniformBuffer->AbortFrame(frameSlotIndex);
			return false;
		}
		VkResult result = vkResetDescriptorPool(m_Device->Get(), slot.m_Pool, 0);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"vkResetDescriptorPool(set 0) failed with {}.", ToString(result));
			(void)m_UniformBuffer->AbortFrame(frameSlotIndex);
			return false;
		}
		slot.m_Set = VK_NULL_HANDLE;
		const VkDescriptorSetLayout set0Layout = m_Layout->GetSet0Layout();
		VkDescriptorSetAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateInfo.descriptorPool = slot.m_Pool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &set0Layout;
		result = vkAllocateDescriptorSets(m_Device->Get(), &allocateInfo, &slot.m_Set);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"vkAllocateDescriptorSets(set 0) failed with {}.", ToString(result));
			(void)m_UniformBuffer->AbortFrame(frameSlotIndex);
			return false;
		}

		const VulkanBindingLayoutPlan& plan = m_Layout->GetPlan();
		std::vector<VkDescriptorBufferInfo> bufferInfos;
		std::vector<VkWriteDescriptorSet> writes;
		bufferInfos.reserve(plan.m_DynamicOffsetCount);
		writes.reserve(plan.m_DynamicOffsetCount);
		for (uint32_t bindingIndex = 0; bindingIndex < plan.m_Set0BindingCount; ++bindingIndex)
		{
			const VulkanSet0BindingPlan& binding = plan.m_Set0Bindings[bindingIndex];
			if (binding.m_DescriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
			{
				continue;
			}
			bufferInfos.push_back({
				.buffer = m_UniformBuffer->GetNativeBuffer(frameSlotIndex),
				.offset = 0,
				.range = binding.m_SizeInBytes,
				});
			writes.push_back({
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = slot.m_Set,
				.dstBinding = binding.m_Binding,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.pBufferInfo = &bufferInfos.back(),
				});
		}
		if (!writes.empty())
		{
			vkUpdateDescriptorSets(m_Device->Get(),
				static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}
		slot.m_LastResetGeneration = frameGeneration;
		slot.m_LastSubmittedFence = {};
		slot.m_Active = true;
		return true;
	}

	bool VulkanSet0DynamicUniformFrames::EndFrame(
		uint32_t frameSlotIndex, const RHIFencePoint& submittedFence) noexcept
	{
		if (m_Device == nullptr || frameSlotIndex >= m_FrameSlots.size() ||
			!m_Device->RequireOwnerThread("VulkanSet0DynamicUniformFrames::EndFrame") ||
			!submittedFence.IsValid() || !m_FrameSlots[frameSlotIndex].m_Active ||
			!m_UniformBuffer->EndFrame(frameSlotIndex, submittedFence))
		{
			return false;
		}
		FrameSlot& slot = m_FrameSlots[frameSlotIndex];
		slot.m_LastSubmittedFence = submittedFence;
		slot.m_Active = false;
		return true;
	}

	bool VulkanSet0DynamicUniformFrames::AbortFrame(uint32_t frameSlotIndex) noexcept
	{
		if (m_Device == nullptr || frameSlotIndex >= m_FrameSlots.size() ||
			!m_Device->RequireOwnerThread("VulkanSet0DynamicUniformFrames::AbortFrame") ||
			!m_FrameSlots[frameSlotIndex].m_Active ||
			!m_UniformBuffer->AbortFrame(frameSlotIndex))
		{
			return false;
		}
		FrameSlot& slot = m_FrameSlots[frameSlotIndex];
		slot.m_Set = VK_NULL_HANDLE;
		slot.m_Active = false;
		return true;
	}

	VkDescriptorSet VulkanSet0DynamicUniformFrames::GetDescriptorSet(
		uint32_t frameSlotIndex) const noexcept
	{
		return frameSlotIndex < m_FrameSlots.size() && m_FrameSlots[frameSlotIndex].m_Active
			? m_FrameSlots[frameSlotIndex].m_Set
			: VK_NULL_HANDLE;
	}

	VkDescriptorSet VulkanSet0DynamicUniformFrames::AllocateDescriptorSet(uint32_t frameSlotIndex,
		std::span<const VulkanSet0BufferBinding> bufferBindings) noexcept
	{
		if (m_Device == nullptr || m_Layout == nullptr || m_UniformBuffer == nullptr ||
			frameSlotIndex >= m_FrameSlots.size() || !m_FrameSlots[frameSlotIndex].m_Active ||
			!m_Device->RequireOwnerThread("VulkanSet0DynamicUniformFrames::AllocateDescriptorSet"))
		{
			return VK_NULL_HANDLE;
		}

		FrameSlot& slot = m_FrameSlots[frameSlotIndex];
		const VkDescriptorSetLayout layout = m_Layout->GetSet0Layout();
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		const VkDescriptorSetAllocateInfo allocateInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = slot.m_Pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &layout,
		};
		const VkResult result =
			vkAllocateDescriptorSets(m_Device->Get(), &allocateInfo, &descriptorSet);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"vkAllocateDescriptorSets(set 0 snapshot) failed with {}.", ToString(result));
			return VK_NULL_HANDLE;
		}

		const VulkanBindingLayoutPlan& plan = m_Layout->GetPlan();
		std::vector<VkDescriptorBufferInfo> bufferInfos;
		std::vector<VkWriteDescriptorSet> writes;
		bufferInfos.reserve(plan.m_DynamicOffsetCount + bufferBindings.size());
		writes.reserve(plan.m_DynamicOffsetCount + bufferBindings.size());
		for (uint32_t bindingIndex = 0; bindingIndex < plan.m_Set0BindingCount; ++bindingIndex)
		{
			const VulkanSet0BindingPlan& binding = plan.m_Set0Bindings[bindingIndex];
			if (binding.m_DescriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
			{
				continue;
			}
			bufferInfos.push_back({
				.buffer = m_UniformBuffer->GetNativeBuffer(frameSlotIndex),
				.offset = 0,
				.range = binding.m_SizeInBytes,
				});
			writes.push_back({
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptorSet,
				.dstBinding = binding.m_Binding,
				.descriptorCount = 1,
				.descriptorType = binding.m_DescriptorType,
				.pBufferInfo = &bufferInfos.back(),
				});
		}

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
		return descriptorSet;
	}
}
