#include "Core/Precompiled.h"
#include "Graphics/RHI/Vulkan/VulkanDescriptorManager.h"
#include "Graphics/RHI/RHIDescriptorCapacityContract.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanGlobalDescriptorLayout.h"
#include "Graphics/RHI/Vulkan/VulkanShaderBindingABI.h"
#include "Graphics/RHI/Vulkan/VulkanTimelineFence.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <algorithm>
#include <unordered_set>

namespace gglab
{
	VulkanDescriptorIndexArena::VulkanDescriptorIndexArena(uint32_t capacity) noexcept :
		m_Capacity(capacity), m_Allocated(capacity, 0)
	{
		m_FreeIndices.reserve(capacity);
		for (uint32_t index = capacity; index > 0; --index)
		{
			m_FreeIndices.push_back(index - 1);
		}
	}

	std::optional<uint32_t> VulkanDescriptorIndexArena::Allocate() noexcept
	{
		if (m_FreeIndices.empty())
		{
			return std::nullopt;
		}
		const uint32_t index = m_FreeIndices.back();
		m_FreeIndices.pop_back();
		GGLAB_ASSERT_MSG(m_Allocated[index] == 0,
			"Vulkan descriptor arena free list contains an allocated index.");
		m_Allocated[index] = 1;
		++m_LiveCount;
		return index;
	}

	void VulkanDescriptorIndexArena::Release(uint32_t index) noexcept
	{
		if (index >= m_Capacity || m_Allocated[index] == 0)
		{
			return;
		}
		m_Allocated[index] = 0;
		m_FreeIndices.push_back(index);
		--m_LiveCount;
	}

	VulkanDescriptorPublicationArena::VulkanDescriptorPublicationArena(uint32_t capacity) noexcept :
		m_Indices(capacity), m_Slots(capacity)
	{
	}

	std::optional<uint32_t> VulkanDescriptorPublicationArena::Allocate() noexcept
	{
		const std::optional<uint32_t> index = m_Indices.Allocate();
		if (!index)
		{
			return std::nullopt;
		}
		Slot& slot = m_Slots[*index];
		GGLAB_ASSERT_MSG(slot.m_State == VulkanDescriptorPublicationState::Free,
			"Allocated Vulkan descriptor publication slot is not free.");
		slot = {};
		slot.m_State = VulkanDescriptorPublicationState::AllocatedUnpublished;
		m_HighWaterMark = std::max(m_HighWaterMark, m_Indices.GetLiveCount());
		return index;
	}

	bool VulkanDescriptorPublicationArena::MarkDescriptorReady(
		uint32_t index, std::shared_ptr<void> backing) noexcept
	{
		if (!IsIndexInRange(index) || !backing ||
			m_Slots[index].m_State != VulkanDescriptorPublicationState::AllocatedUnpublished)
		{
			RecordInvalidTransition();
			return false;
		}
		Slot& slot = m_Slots[index];
		slot.m_Backing = std::move(backing);
		slot.m_State = VulkanDescriptorPublicationState::DescriptorReady;
		return true;
	}

	bool VulkanDescriptorPublicationArena::Publish(
		uint32_t index, uint64_t publicationGeneration) noexcept
	{
		if (!IsIndexInRange(index) || publicationGeneration == 0 ||
			m_Slots[index].m_State != VulkanDescriptorPublicationState::DescriptorReady)
		{
			RecordInvalidTransition();
			return false;
		}
		Slot& slot = m_Slots[index];
		slot.m_PublicationGeneration = publicationGeneration;
		slot.m_State = VulkanDescriptorPublicationState::Live;
		return true;
	}

	bool VulkanDescriptorPublicationArena::Retire(uint32_t index,
		const RHIDescriptorRetirement& retirement,
		std::span<const RHIFencePoint> ownerRetirementPoints) noexcept
	{
		if (!IsIndexInRange(index) ||
			m_Slots[index].m_State != VulkanDescriptorPublicationState::Live ||
			m_Slots[index].m_RetirementRequested)
		{
			RecordInvalidTransition();
			return false;
		}

		Slot& slot = m_Slots[index];
		slot.m_RetirementPoints.clear();
		AppendRetirementPoints(slot, retirement, ownerRetirementPoints);
		slot.m_State = VulkanDescriptorPublicationState::Retired;
		return true;
	}

	bool VulkanDescriptorPublicationArena::RequestRetirement(uint32_t index,
		uint64_t lastReachableGeneration, const RHIDescriptorRetirement& retirement,
		std::span<const RHIFencePoint> ownerRetirementPoints) noexcept
	{
		if (!IsIndexInRange(index) || lastReachableGeneration == 0 ||
			m_Slots[index].m_State != VulkanDescriptorPublicationState::Live ||
			lastReachableGeneration < m_Slots[index].m_PublicationGeneration)
		{
			RecordInvalidTransition();
			return false;
		}

		Slot& slot = m_Slots[index];
		if (slot.m_RetirementRequested &&
			slot.m_LastReachableGeneration != lastReachableGeneration)
		{
			RecordInvalidTransition();
			return false;
		}
		if (!slot.m_RetirementRequested)
		{
			slot.m_LastReachableGeneration = lastReachableGeneration;
			slot.m_RetirementRequested = true;
		}
		AppendRetirementPoints(slot, retirement, ownerRetirementPoints);
		return true;
	}

	bool VulkanDescriptorPublicationArena::CompleteRetirementRequest(
		uint32_t index, const RHIDescriptorRetirement& retirement) noexcept
	{
		if (!IsIndexInRange(index) ||
			m_Slots[index].m_State != VulkanDescriptorPublicationState::Live ||
			!m_Slots[index].m_RetirementRequested)
		{
			RecordInvalidTransition();
			return false;
		}

		Slot& slot = m_Slots[index];
		AppendRetirementPoints(slot, retirement, {});
		slot.m_RetirementRequested = false;
		slot.m_State = VulkanDescriptorPublicationState::Retired;
		return true;
	}

	bool VulkanDescriptorPublicationArena::JoinRetirement(uint32_t index,
		const RHIDescriptorRetirement& retirement,
		std::span<const RHIFencePoint> ownerRetirementPoints) noexcept
	{
		if (!IsIndexInRange(index) ||
			m_Slots[index].m_State != VulkanDescriptorPublicationState::Retired)
		{
			RecordInvalidTransition();
			return false;
		}
		AppendRetirementPoints(m_Slots[index], retirement, ownerRetirementPoints);
		return true;
	}

	void VulkanDescriptorPublicationArena::AppendRetirementPoints(Slot& slot,
		const RHIDescriptorRetirement& retirement,
		std::span<const RHIFencePoint> ownerRetirementPoints) noexcept
	{
		auto appendPoint = [&slot](const RHIFencePoint& point) noexcept
			{
				if (!point.IsValid())
				{
					return;
				}
				for (RHIFencePoint& existing : slot.m_RetirementPoints)
				{
					if (existing.m_Fence == point.m_Fence)
					{
						existing.m_Value = std::max(existing.m_Value, point.m_Value);
						return;
					}
				}
				slot.m_RetirementPoints.push_back(point);
			};
		appendPoint(retirement.m_LastPossibleGraphicsUse);
		for (const RHIFencePoint& point : ownerRetirementPoints)
		{
			appendPoint(point);
		}
	}

	bool VulkanDescriptorPublicationArena::CancelUnpublished(uint32_t index) noexcept
	{
		if (!IsIndexInRange(index))
		{
			RecordInvalidTransition();
			return false;
		}
		const VulkanDescriptorPublicationState state = m_Slots[index].m_State;
		if (state != VulkanDescriptorPublicationState::AllocatedUnpublished &&
			state != VulkanDescriptorPublicationState::DescriptorReady)
		{
			RecordInvalidTransition();
			return false;
		}
		ReleaseSlot(index);
		return true;
	}

	void VulkanDescriptorPublicationArena::ReleaseCompleted(
		const std::function<bool(const RHIFencePoint&)>& isCompleted) noexcept
	{
		for (uint32_t index = 0; index < m_Slots.size(); ++index)
		{
			const Slot& slot = m_Slots[index];
			if (slot.m_State != VulkanDescriptorPublicationState::Retired)
			{
				continue;
			}
			const bool completed = std::ranges::all_of(slot.m_RetirementPoints,
				[&isCompleted](const RHIFencePoint& point) noexcept
				{
					return isCompleted(point);
				});
			if (completed)
			{
				ReleaseSlot(index);
			}
		}
	}

	void VulkanDescriptorPublicationArena::Reset() noexcept
	{
		const uint32_t capacity = m_Indices.GetCapacity();
		m_Indices = VulkanDescriptorIndexArena(capacity);
		m_Slots.assign(capacity, {});
		m_HighWaterMark = 0;
		m_InvalidTransitionCount = 0;
	}

	VulkanDescriptorPublicationState VulkanDescriptorPublicationArena::GetState(
		uint32_t index) const noexcept
	{
		return IsIndexInRange(index)
			? m_Slots[index].m_State
			: VulkanDescriptorPublicationState::Free;
	}

	uint64_t VulkanDescriptorPublicationArena::GetPublicationGeneration(
		uint32_t index) const noexcept
	{
		return IsIndexInRange(index) ? m_Slots[index].m_PublicationGeneration : 0;
	}

	uint64_t VulkanDescriptorPublicationArena::GetLastReachableGeneration(
		uint32_t index) const noexcept
	{
		return IsIndexInRange(index) ? m_Slots[index].m_LastReachableGeneration : 0;
	}

	bool VulkanDescriptorPublicationArena::IsRetirementRequested(uint32_t index) const noexcept
	{
		return IsIndexInRange(index) && m_Slots[index].m_RetirementRequested;
	}

	const std::shared_ptr<void>& VulkanDescriptorPublicationArena::GetBacking(
		uint32_t index) const noexcept
	{
		static const std::shared_ptr<void> EmptyBacking;
		return IsIndexInRange(index) ? m_Slots[index].m_Backing : EmptyBacking;
	}

	VulkanDescriptorPublicationDiagnostics
		VulkanDescriptorPublicationArena::GetDiagnostics() const noexcept
	{
		VulkanDescriptorPublicationDiagnostics diagnostics{};
		diagnostics.m_Capacity = m_Indices.GetCapacity();
		diagnostics.m_HighWaterMark = m_HighWaterMark;
		diagnostics.m_InvalidTransitionCount = m_InvalidTransitionCount;
		for (const Slot& slot : m_Slots)
		{
			diagnostics.m_RetainedBackingCount += slot.m_Backing ? 1u : 0u;
			diagnostics.m_RetirementRequestedCount += slot.m_RetirementRequested ? 1u : 0u;
			switch (slot.m_State)
			{
			case VulkanDescriptorPublicationState::Free:
				++diagnostics.m_FreeCount;
				break;
			case VulkanDescriptorPublicationState::AllocatedUnpublished:
				++diagnostics.m_AllocatedUnpublishedCount;
				break;
			case VulkanDescriptorPublicationState::DescriptorReady:
				++diagnostics.m_DescriptorReadyCount;
				break;
			case VulkanDescriptorPublicationState::Live:
				++diagnostics.m_LiveCount;
				break;
			case VulkanDescriptorPublicationState::Retired:
				++diagnostics.m_RetiredCount;
				break;
			}
		}
		return diagnostics;
	}

	bool VulkanDescriptorPublicationArena::IsIndexInRange(uint32_t index) const noexcept
	{
		return index < m_Slots.size();
	}

	void VulkanDescriptorPublicationArena::ReleaseSlot(uint32_t index) noexcept
	{
		Slot& slot = m_Slots[index];
		slot = {};
		m_Indices.Release(index);
	}

	VulkanDescriptorPublicationTracker::VulkanDescriptorPublicationTracker(
		uint32_t frameSlotCount) noexcept : m_FrameSlots(frameSlotCount)
	{
	}

	bool VulkanDescriptorPublicationTracker::BeginFrameSnapshot(uint32_t frameSlotIndex,
		const std::function<bool(const RHIFencePoint&)>& isCompleted) noexcept
	{
		if (frameSlotIndex >= m_FrameSlots.size())
		{
			return false;
		}
		FrameSlot& slot = m_FrameSlots[frameSlotIndex];
		if (slot.m_HasSnapshot && (!slot.m_Submitted ||
			!isCompleted(slot.m_SubmittedFence)))
		{
			return false;
		}
		slot = {};
		slot.m_SnapshotGeneration = m_CurrentGeneration;
		slot.m_HasSnapshot = true;
		return true;
	}

	bool VulkanDescriptorPublicationTracker::SubmitFrameSnapshot(
		uint32_t frameSlotIndex, const RHIFencePoint& submittedFence) noexcept
	{
		if (frameSlotIndex >= m_FrameSlots.size() || !submittedFence.IsValid())
		{
			return false;
		}
		FrameSlot& slot = m_FrameSlots[frameSlotIndex];
		if (!slot.m_HasSnapshot || slot.m_Submitted)
		{
			return false;
		}
		slot.m_SubmittedFence = submittedFence;
		slot.m_Submitted = true;
		return true;
	}

	bool VulkanDescriptorPublicationTracker::AbortFrameSnapshot(uint32_t frameSlotIndex) noexcept
	{
		if (frameSlotIndex >= m_FrameSlots.size())
		{
			return false;
		}
		FrameSlot& slot = m_FrameSlots[frameSlotIndex];
		if (!slot.m_HasSnapshot || slot.m_Submitted)
		{
			return false;
		}
		slot = {};
		return true;
	}

	uint64_t VulkanDescriptorPublicationTracker::PublishReplacement() noexcept
	{
		if (m_CurrentGeneration == UINT64_MAX)
		{
			return 0;
		}
		return ++m_CurrentGeneration;
	}

	bool VulkanDescriptorPublicationTracker::TryDeriveRetirement(
		uint64_t publicationGeneration, RHIDescriptorRetirement& outRetirement) const noexcept
	{
		outRetirement = {};
		if (publicationGeneration == 0 || publicationGeneration >= m_CurrentGeneration)
		{
			return false;
		}
		for (const FrameSlot& slot : m_FrameSlots)
		{
			if (!slot.m_HasSnapshot || slot.m_SnapshotGeneration > publicationGeneration)
			{
				continue;
			}
			if (!slot.m_Submitted)
			{
				return false;
			}
			RHIFencePoint& lastUse = outRetirement.m_LastPossibleGraphicsUse;
			if (!lastUse.IsValid())
			{
				lastUse = slot.m_SubmittedFence;
			}
			else if (lastUse.m_Fence != slot.m_SubmittedFence.m_Fence)
			{
				outRetirement = {};
				return false;
			}
			else
			{
				lastUse.m_Value = std::max(lastUse.m_Value, slot.m_SubmittedFence.m_Value);
			}
		}
		return true;
	}

	void VulkanDescriptorPublicationTracker::ResetFrameSlots(uint32_t frameSlotCount) noexcept
	{
		m_FrameSlots.assign(frameSlotCount, {});
	}

	bool VulkanDescriptorPublicationTracker::HasUnsubmittedSnapshots() const noexcept
	{
		return std::ranges::any_of(m_FrameSlots, [](const FrameSlot& slot) noexcept
			{
				return slot.m_HasSnapshot && !slot.m_Submitted;
			});
	}

	VulkanDescriptorBacking::VulkanDescriptorBacking(VkDevice device, VkImageView imageView,
		std::shared_ptr<void> parentOwner, uint64_t estimatedRetainedBytes) noexcept :
		m_Device(device), m_Kind(Kind::ImageView), m_ImageView(imageView),
		m_ParentOwner(std::move(parentOwner)), m_EstimatedRetainedBytes(estimatedRetainedBytes)
	{
	}

	VulkanDescriptorBacking::VulkanDescriptorBacking(VkDevice device, VkBufferView bufferView,
		std::shared_ptr<void> parentOwner, uint64_t estimatedRetainedBytes) noexcept :
		m_Device(device), m_Kind(Kind::BufferView), m_BufferView(bufferView),
		m_ParentOwner(std::move(parentOwner)), m_EstimatedRetainedBytes(estimatedRetainedBytes)
	{
	}

	VulkanDescriptorBacking::VulkanDescriptorBacking(VkDevice device, VkSampler sampler) noexcept :
		m_Device(device), m_Kind(Kind::Sampler), m_Sampler(sampler)
	{
	}

	VulkanDescriptorBacking::~VulkanDescriptorBacking() noexcept
	{
		if (m_Device == VK_NULL_HANDLE)
		{
			return;
		}
		switch (m_Kind)
		{
		case Kind::ImageView:
			if (m_ImageView != VK_NULL_HANDLE)
			{
				vkDestroyImageView(m_Device, m_ImageView, nullptr);
			}
			break;
		case Kind::BufferView:
			if (m_BufferView != VK_NULL_HANDLE)
			{
				vkDestroyBufferView(m_Device, m_BufferView, nullptr);
			}
			break;
		case Kind::Sampler:
			if (m_Sampler != VK_NULL_HANDLE)
			{
				vkDestroySampler(m_Device, m_Sampler, nullptr);
			}
			break;
		}
	}

	VulkanDescriptorManager::~VulkanDescriptorManager() noexcept
	{
		Finalize();
	}

	bool VulkanDescriptorManager::Initialize(VulkanDevice* device) noexcept
	{
		if (m_Device != nullptr || device == nullptr)
		{
			return false;
		}
		m_Device = device;
		if (!CheckOwnerThread("VulkanDescriptorManager::Initialize"))
		{
			m_Device = nullptr;
			return false;
		}
		m_Resources = VulkanDescriptorPublicationArena(
			GGLabDescriptorCapacityContract.m_ResourceDescriptorCount);
		m_Samplers = VulkanDescriptorPublicationArena(
			GGLabDescriptorCapacityContract.m_SamplerDescriptorCount);
		if (!CreateGlobalSet())
		{
			Finalize();
			return false;
		}
		return true;
	}

	void VulkanDescriptorManager::Finalize() noexcept
	{
		if (m_Device == nullptr)
		{
			return;
		}
		if (!CheckOwnerThread("VulkanDescriptorManager::Finalize"))
		{
			return;
		}
		if (m_GlobalPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(m_Device->Get(), m_GlobalPool, nullptr);
			m_GlobalPool = VK_NULL_HANDLE;
			m_GlobalSet = VK_NULL_HANDLE;
		}
		if (m_GlobalSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(m_Device->Get(), m_GlobalSetLayout, nullptr);
			m_GlobalSetLayout = VK_NULL_HANDLE;
		}
		m_Resources.Reset();
		m_Samplers.Reset();
		m_PublicationTracker = VulkanDescriptorPublicationTracker{};
		m_PendingResourceRetirements.clear();
		m_PendingSamplerRetirements.clear();
		m_LayoutSupported = false;
		m_Device = nullptr;
	}

	std::optional<uint32_t> VulkanDescriptorManager::AllocateResourceDescriptor() noexcept
	{
		return CheckOwnerThread("VulkanDescriptorManager::AllocateResourceDescriptor")
			? m_Resources.Allocate()
			: std::nullopt;
	}

	std::optional<uint32_t> VulkanDescriptorManager::AllocateSamplerDescriptor() noexcept
	{
		return CheckOwnerThread("VulkanDescriptorManager::AllocateSamplerDescriptor")
			? m_Samplers.Allocate()
			: std::nullopt;
	}

	bool VulkanDescriptorManager::WriteSampledImage(uint32_t index,
		const std::shared_ptr<VulkanDescriptorBacking>& backing) noexcept
	{
		return WriteImage(index, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, backing);
	}

	bool VulkanDescriptorManager::WriteStorageImage(uint32_t index,
		const std::shared_ptr<VulkanDescriptorBacking>& backing) noexcept
	{
		return WriteImage(index, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			VK_IMAGE_LAYOUT_GENERAL, backing);
	}

	bool VulkanDescriptorManager::WriteSampler(uint32_t index,
		const std::shared_ptr<VulkanDescriptorBacking>& backing) noexcept
	{
		if (!CheckOwnerThread("VulkanDescriptorManager::WriteSampler") || !backing ||
			backing->GetKind() != VulkanDescriptorBacking::Kind::Sampler ||
			backing->GetSampler() == VK_NULL_HANDLE ||
			m_Samplers.GetState(index) != VulkanDescriptorPublicationState::AllocatedUnpublished)
		{
			return false;
		}
		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = backing->GetSampler();
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = m_GlobalSet;
		write.dstBinding = GGLabVulkanShaderBindingABI.m_SamplerHeapBinding;
		write.dstArrayElement = index;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(m_Device->Get(), 1, &write, 0, nullptr);
		return m_Samplers.MarkDescriptorReady(index, backing);
	}

	bool VulkanDescriptorManager::PublishResource(uint32_t index) noexcept
	{
		return CheckOwnerThread("VulkanDescriptorManager::PublishResource") &&
			m_Resources.Publish(index, m_PublicationTracker.GetCurrentGeneration());
	}

	bool VulkanDescriptorManager::PublishSampler(uint32_t index) noexcept
	{
		return CheckOwnerThread("VulkanDescriptorManager::PublishSampler") &&
			m_Samplers.Publish(index, m_PublicationTracker.GetCurrentGeneration());
	}

	bool VulkanDescriptorManager::InitializeFrameTracking(uint32_t frameSlotCount) noexcept
	{
		if (!CheckOwnerThread("VulkanDescriptorManager::InitializeFrameTracking") ||
			frameSlotCount == 0 || m_PublicationTracker.GetFrameSlotCount() != 0)
		{
			return false;
		}
		m_PublicationTracker.ResetFrameSlots(frameSlotCount);
		return true;
	}

	bool VulkanDescriptorManager::DetachFrameTracking() noexcept
	{
		if (!CheckOwnerThread("VulkanDescriptorManager::DetachFrameTracking"))
		{
			return false;
		}
		if (m_PublicationTracker.HasUnsubmittedSnapshots())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Cannot detach Vulkan descriptor frame tracking with an unsubmitted snapshot.");
			return false;
		}
		ProcessPendingRetirements();
		m_PublicationTracker.ResetFrameSlots(0);
		return true;
	}

	bool VulkanDescriptorManager::BeginFrameSnapshot(uint32_t frameSlotIndex) noexcept
	{
		if (!CheckOwnerThread("VulkanDescriptorManager::BeginFrameSnapshot"))
		{
			return false;
		}
		auto isCompleted = [this](const RHIFencePoint& point) noexcept
			{
				return m_Device->IsFencePointCompleted(point);
			};
		return m_PublicationTracker.BeginFrameSnapshot(frameSlotIndex, isCompleted);
	}

	bool VulkanDescriptorManager::SubmitFrameSnapshot(uint32_t frameSlotIndex,
		const RHIFencePoint& submittedFence) noexcept
	{
		if (!CheckOwnerThread("VulkanDescriptorManager::SubmitFrameSnapshot"))
		{
			return false;
		}
		const VulkanTimelineFence* timeline = m_Device->GetGraphicsTimeline();
		if (timeline == nullptr || submittedFence.m_Fence != timeline->GetRHIHandle() ||
			submittedFence.m_Value > timeline->GetCurrentSignalValue() ||
			!m_PublicationTracker.SubmitFrameSnapshot(frameSlotIndex, submittedFence))
		{
			return false;
		}
		ProcessPendingRetirements();
		return true;
	}

	bool VulkanDescriptorManager::AbortFrameSnapshot(uint32_t frameSlotIndex) noexcept
	{
		if (!CheckOwnerThread("VulkanDescriptorManager::AbortFrameSnapshot") ||
			!m_PublicationTracker.AbortFrameSnapshot(frameSlotIndex))
		{
			return false;
		}
		ProcessPendingRetirements();
		return true;
	}

	bool VulkanDescriptorManager::RetireResource(uint32_t index,
		const RHIDescriptorRetirement& retirement,
		std::span<const RHIFencePoint> ownerRetirementPoints) noexcept
	{
		if (!CheckOwnerThread("VulkanDescriptorManager::RetireResource"))
		{
			return false;
		}
		const bool wasRequested = m_Resources.IsRetirementRequested(index);
		const bool retired = RetireDescriptor(
			m_Resources, index, retirement, ownerRetirementPoints);
		if (retired && !wasRequested && m_Resources.IsRetirementRequested(index))
		{
			m_PendingResourceRetirements.push_back(index);
			ProcessPendingRetirements(m_Resources, m_PendingResourceRetirements);
		}
		return retired;
	}

	bool VulkanDescriptorManager::RetireSampler(uint32_t index,
		const RHIDescriptorRetirement& retirement,
		std::span<const RHIFencePoint> ownerRetirementPoints) noexcept
	{
		if (!CheckOwnerThread("VulkanDescriptorManager::RetireSampler"))
		{
			return false;
		}
		const bool wasRequested = m_Samplers.IsRetirementRequested(index);
		const bool retired = RetireDescriptor(
			m_Samplers, index, retirement, ownerRetirementPoints);
		if (retired && !wasRequested && m_Samplers.IsRetirementRequested(index))
		{
			m_PendingSamplerRetirements.push_back(index);
			ProcessPendingRetirements(m_Samplers, m_PendingSamplerRetirements);
		}
		return retired;
	}

	bool VulkanDescriptorManager::RetireDescriptor(VulkanDescriptorPublicationArena& arena,
		uint32_t index, const RHIDescriptorRetirement& retirement,
		std::span<const RHIFencePoint> ownerRetirementPoints) noexcept
	{
		const VulkanDescriptorPublicationState state = arena.GetState(index);
		if (state == VulkanDescriptorPublicationState::Retired)
		{
			return arena.JoinRetirement(index, retirement, ownerRetirementPoints);
		}
		if (state != VulkanDescriptorPublicationState::Live)
		{
			return arena.CancelUnpublished(index);
		}
		if (arena.IsRetirementRequested(index))
		{
			return arena.RequestRetirement(index, arena.GetLastReachableGeneration(index),
				retirement, ownerRetirementPoints);
		}

		const uint64_t lastReachableGeneration = m_PublicationTracker.GetCurrentGeneration();
		if (m_PublicationTracker.PublishReplacement() == 0 ||
			!arena.RequestRetirement(index, lastReachableGeneration,
				retirement, ownerRetirementPoints))
		{
			return false;
		}
		return true;
	}

	void VulkanDescriptorManager::ProcessPendingRetirements(
		VulkanDescriptorPublicationArena& arena,
		std::vector<uint32_t>& pendingIndices) noexcept
	{
		std::erase_if(pendingIndices, [this, &arena](uint32_t index) noexcept
			{
				if (!arena.IsRetirementRequested(index))
				{
					return true;
				}
				RHIDescriptorRetirement retirement{};
				if (!m_PublicationTracker.TryDeriveRetirement(
					arena.GetLastReachableGeneration(index), retirement))
				{
					return false;
				}
				const bool completed = arena.CompleteRetirementRequest(index, retirement);
				GGLAB_ASSERT_MSG(completed,
					"Vulkan descriptor retirement request changed state while being processed.");
				return completed;
			});
	}

	void VulkanDescriptorManager::ProcessPendingRetirements() noexcept
	{
		ProcessPendingRetirements(m_Resources, m_PendingResourceRetirements);
		ProcessPendingRetirements(m_Samplers, m_PendingSamplerRetirements);
	}

	void VulkanDescriptorManager::RetireCompleted() noexcept
	{
		if (!CheckOwnerThread("VulkanDescriptorManager::RetireCompleted"))
		{
			return;
		}
		ProcessPendingRetirements();
		auto isCompleted = [this](const RHIFencePoint& point) noexcept
			{
				return m_Device->IsFencePointCompleted(point);
			};
		m_Resources.ReleaseCompleted(isCompleted);
		m_Samplers.ReleaseCompleted(isCompleted);
	}

	VulkanDescriptorPublicationState VulkanDescriptorManager::GetResourceState(
		uint32_t index) const noexcept
	{
		return CheckOwnerThread("VulkanDescriptorManager::GetResourceState")
			? m_Resources.GetState(index)
			: VulkanDescriptorPublicationState::Free;
	}

	VulkanDescriptorPublicationState VulkanDescriptorManager::GetSamplerState(
		uint32_t index) const noexcept
	{
		return CheckOwnerThread("VulkanDescriptorManager::GetSamplerState")
			? m_Samplers.GetState(index)
			: VulkanDescriptorPublicationState::Free;
	}

	VulkanDescriptorPublicationDiagnostics VulkanDescriptorManager::GetResourceDiagnostics()
		const noexcept
	{
		if (!CheckOwnerThread("VulkanDescriptorManager::GetResourceDiagnostics"))
		{
			return {};
		}
		VulkanDescriptorPublicationDiagnostics diagnostics = m_Resources.GetDiagnostics();
		std::unordered_set<const void*> retainedParents;
		for (uint32_t index = 0; index < diagnostics.m_Capacity; ++index)
		{
			const std::shared_ptr<void>& erasedBacking = m_Resources.GetBacking(index);
			if (!erasedBacking)
			{
				continue;
			}
			const auto backing = std::static_pointer_cast<VulkanDescriptorBacking>(erasedBacking);
			const std::shared_ptr<void>& parent = backing->GetParentOwner();
			const void* retainedIdentity = parent ? parent.get() : backing.get();
			if (retainedParents.insert(retainedIdentity).second)
			{
				diagnostics.m_EstimatedRetainedBytes += backing->GetEstimatedRetainedBytes();
			}
		}
		return diagnostics;
	}

	VulkanDescriptorPublicationDiagnostics VulkanDescriptorManager::GetSamplerDiagnostics()
		const noexcept
	{
		return CheckOwnerThread("VulkanDescriptorManager::GetSamplerDiagnostics")
			? m_Samplers.GetDiagnostics()
			: VulkanDescriptorPublicationDiagnostics{};
	}

	VkDescriptorSetLayout VulkanDescriptorManager::GetGlobalSetLayout() const noexcept
	{
		return CheckOwnerThread("VulkanDescriptorManager::GetGlobalSetLayout")
			? m_GlobalSetLayout
			: VK_NULL_HANDLE;
	}

	VkDescriptorSet VulkanDescriptorManager::GetGlobalSet() const noexcept
	{
		return CheckOwnerThread("VulkanDescriptorManager::GetGlobalSet")
			? m_GlobalSet
			: VK_NULL_HANDLE;
	}

	bool VulkanDescriptorManager::IsLayoutSupported() const noexcept
	{
		return CheckOwnerThread("VulkanDescriptorManager::IsLayoutSupported") &&
			m_LayoutSupported;
	}

	bool VulkanDescriptorManager::CheckOwnerThread(const char* operation) const noexcept
	{
		return m_Device != nullptr && m_Device->RequireOwnerThread(operation);
	}

	bool VulkanDescriptorManager::CreateGlobalSet() noexcept
	{
		const VulkanGlobalDescriptorLayoutPlan plan;
		const VkDescriptorSetLayoutCreateInfo& layoutInfo = plan.GetLayoutInfo();

		VkDescriptorSetLayoutSupport layoutSupport{};
		layoutSupport.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT;
		vkGetDescriptorSetLayoutSupport(m_Device->Get(), &layoutInfo, &layoutSupport);
		m_LayoutSupported = layoutSupport.supported == VK_TRUE;
		if (!m_LayoutSupported)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Vulkan global descriptor-set layout is not supported at the required capacity.");
			return false;
		}

		VkResult result =
			vkCreateDescriptorSetLayout(m_Device->Get(), &layoutInfo, nullptr, &m_GlobalSetLayout);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"vkCreateDescriptorSetLayout(global) failed with {}.", ToString(result));
			return false;
		}

		const auto& poolSizes = plan.GetPoolSizes();
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.pNext = &plan.GetMutableInfo();
		poolInfo.flags = plan.GetPoolCreateFlags();
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		result = vkCreateDescriptorPool(m_Device->Get(), &poolInfo, nullptr, &m_GlobalPool);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"vkCreateDescriptorPool(global) failed with {}.", ToString(result));
			return false;
		}

		VkDescriptorSetAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateInfo.descriptorPool = m_GlobalPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &m_GlobalSetLayout;
		result = vkAllocateDescriptorSets(m_Device->Get(), &allocateInfo, &m_GlobalSet);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"vkAllocateDescriptorSets(global) failed with {}.", ToString(result));
			return false;
		}

		SetVulkanObjectDebugName(m_Device->Get(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
			reinterpret_cast<uint64_t>(m_GlobalSetLayout), "Vulkan.GlobalDescriptorSetLayout");
		SetVulkanObjectDebugName(m_Device->Get(), VK_OBJECT_TYPE_DESCRIPTOR_POOL,
			reinterpret_cast<uint64_t>(m_GlobalPool), "Vulkan.GlobalDescriptorPool");
		SetVulkanObjectDebugName(m_Device->Get(), VK_OBJECT_TYPE_DESCRIPTOR_SET,
			reinterpret_cast<uint64_t>(m_GlobalSet), "Vulkan.GlobalDescriptorSet");
		return true;
	}

	bool VulkanDescriptorManager::WriteImage(uint32_t index, VkDescriptorType descriptorType,
		VkImageLayout imageLayout,
		const std::shared_ptr<VulkanDescriptorBacking>& backing) noexcept
	{
		if (!CheckOwnerThread("VulkanDescriptorManager::WriteImage") || !backing ||
			backing->GetKind() != VulkanDescriptorBacking::Kind::ImageView ||
			backing->GetImageView() == VK_NULL_HANDLE ||
			m_Resources.GetState(index) !=
			VulkanDescriptorPublicationState::AllocatedUnpublished)
		{
			return false;
		}
		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageView = backing->GetImageView();
		imageInfo.imageLayout = imageLayout;
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = m_GlobalSet;
		write.dstBinding = GGLabVulkanShaderBindingABI.m_ResourceHeapBinding;
		write.dstArrayElement = index;
		write.descriptorCount = 1;
		write.descriptorType = descriptorType;
		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(m_Device->Get(), 1, &write, 0, nullptr);
		return m_Resources.MarkDescriptorReady(index, backing);
	}
}
