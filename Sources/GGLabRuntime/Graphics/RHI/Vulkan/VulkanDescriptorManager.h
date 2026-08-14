#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHIFence.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace gglab
{
	class VulkanDevice;

	class VulkanDescriptorIndexArena
	{
	public:
		explicit VulkanDescriptorIndexArena(uint32_t capacity) noexcept;

		[[nodiscard]] std::optional<uint32_t> Allocate() noexcept;
		void Release(uint32_t index) noexcept;

		[[nodiscard]] uint32_t GetCapacity() const noexcept { return m_Capacity; }
		[[nodiscard]] uint32_t GetLiveCount() const noexcept { return m_LiveCount; }

	private:
		uint32_t m_Capacity = 0;
		uint32_t m_LiveCount = 0;
		std::vector<uint32_t> m_FreeIndices;
		std::vector<uint8_t> m_Allocated;
	};

	enum class VulkanDescriptorPublicationState : uint8_t
	{
		Free,
		AllocatedUnpublished,
		DescriptorReady,
		Live,
		Retired,
	};

	struct RHIDescriptorRetirement
	{
		RHIFencePoint m_LastPossibleGraphicsUse{};
	};

	struct VulkanDescriptorPublicationDiagnostics
	{
		uint32_t m_Capacity = 0;
		uint32_t m_FreeCount = 0;
		uint32_t m_AllocatedUnpublishedCount = 0;
		uint32_t m_DescriptorReadyCount = 0;
		uint32_t m_LiveCount = 0;
		uint32_t m_RetiredCount = 0;
		uint32_t m_RetirementRequestedCount = 0;
		uint32_t m_RetainedBackingCount = 0;
		uint64_t m_EstimatedRetainedBytes = 0;
		uint32_t m_HighWaterMark = 0;
		uint64_t m_InvalidTransitionCount = 0;
	};

	class VulkanDescriptorPublicationArena
	{
	public:
		explicit VulkanDescriptorPublicationArena(uint32_t capacity = 0) noexcept;

		[[nodiscard]] std::optional<uint32_t> Allocate() noexcept;
		[[nodiscard]] bool MarkDescriptorReady(
			uint32_t index, std::shared_ptr<void> backing) noexcept;
		[[nodiscard]] bool Publish(uint32_t index, uint64_t publicationGeneration) noexcept;
		[[nodiscard]] bool Retire(uint32_t index, const RHIDescriptorRetirement& retirement,
			std::span<const RHIFencePoint> ownerRetirementPoints = {}) noexcept;
		[[nodiscard]] bool RequestRetirement(uint32_t index, uint64_t lastReachableGeneration,
			const RHIDescriptorRetirement& retirement,
			std::span<const RHIFencePoint> ownerRetirementPoints = {}) noexcept;
		[[nodiscard]] bool CompleteRetirementRequest(
			uint32_t index, const RHIDescriptorRetirement& retirement) noexcept;
		[[nodiscard]] bool JoinRetirement(uint32_t index,
			const RHIDescriptorRetirement& retirement,
			std::span<const RHIFencePoint> ownerRetirementPoints = {}) noexcept;
		[[nodiscard]] bool CancelUnpublished(uint32_t index) noexcept;
		void ReleaseCompleted(
			const std::function<bool(const RHIFencePoint&)>& isCompleted) noexcept;
		void Reset() noexcept;

		[[nodiscard]] VulkanDescriptorPublicationState GetState(uint32_t index) const noexcept;
		[[nodiscard]] uint64_t GetPublicationGeneration(uint32_t index) const noexcept;
		[[nodiscard]] uint64_t GetLastReachableGeneration(uint32_t index) const noexcept;
		[[nodiscard]] bool IsRetirementRequested(uint32_t index) const noexcept;
		[[nodiscard]] uint32_t GetCapacity() const noexcept { return m_Indices.GetCapacity(); }
		[[nodiscard]] const std::shared_ptr<void>& GetBacking(uint32_t index) const noexcept;
		[[nodiscard]] VulkanDescriptorPublicationDiagnostics GetDiagnostics() const noexcept;

	private:
		struct Slot
		{
			VulkanDescriptorPublicationState m_State = VulkanDescriptorPublicationState::Free;
			uint64_t m_PublicationGeneration = 0;
			uint64_t m_LastReachableGeneration = 0;
			std::vector<RHIFencePoint> m_RetirementPoints;
			std::shared_ptr<void> m_Backing;
			bool m_RetirementRequested = false;
		};

		[[nodiscard]] bool IsIndexInRange(uint32_t index) const noexcept;
		void AppendRetirementPoints(Slot& slot, const RHIDescriptorRetirement& retirement,
			std::span<const RHIFencePoint> ownerRetirementPoints) noexcept;
		void ReleaseSlot(uint32_t index) noexcept;
		void RecordInvalidTransition() noexcept { ++m_InvalidTransitionCount; }

		VulkanDescriptorIndexArena m_Indices;
		std::vector<Slot> m_Slots;
		uint32_t m_HighWaterMark = 0;
		uint64_t m_InvalidTransitionCount = 0;
	};

	class VulkanDescriptorPublicationTracker
	{
	public:
		explicit VulkanDescriptorPublicationTracker(uint32_t frameSlotCount = 0) noexcept;

		[[nodiscard]] bool BeginFrameSnapshot(uint32_t frameSlotIndex,
			const std::function<bool(const RHIFencePoint&)>& isCompleted) noexcept;
		[[nodiscard]] bool SubmitFrameSnapshot(
			uint32_t frameSlotIndex, const RHIFencePoint& submittedFence) noexcept;
		[[nodiscard]] bool AbortFrameSnapshot(uint32_t frameSlotIndex) noexcept;
		[[nodiscard]] uint64_t PublishReplacement() noexcept;
		[[nodiscard]] bool TryDeriveRetirement(
			uint64_t publicationGeneration, RHIDescriptorRetirement& outRetirement) const noexcept;
		void ResetFrameSlots(uint32_t frameSlotCount) noexcept;

		[[nodiscard]] uint64_t GetCurrentGeneration() const noexcept
		{
			return m_CurrentGeneration;
		}
		[[nodiscard]] uint32_t GetFrameSlotCount() const noexcept
		{
			return static_cast<uint32_t>(m_FrameSlots.size());
		}
		[[nodiscard]] bool HasUnsubmittedSnapshots() const noexcept;

	private:
		struct FrameSlot
		{
			uint64_t m_SnapshotGeneration = 0;
			RHIFencePoint m_SubmittedFence{};
			bool m_HasSnapshot = false;
			bool m_Submitted = false;
		};

		std::vector<FrameSlot> m_FrameSlots;
		uint64_t m_CurrentGeneration = 1;
	};

	class VulkanDescriptorBacking
	{
	public:
		enum class Kind : uint8_t
		{
			ImageView,
			BufferView,
			Sampler,
		};

		VulkanDescriptorBacking(VkDevice device, VkImageView imageView,
			std::shared_ptr<void> parentOwner, uint64_t estimatedRetainedBytes = 0) noexcept;
		VulkanDescriptorBacking(VkDevice device, VkBufferView bufferView,
			std::shared_ptr<void> parentOwner, uint64_t estimatedRetainedBytes = 0) noexcept;
		VulkanDescriptorBacking(VkDevice device, VkSampler sampler) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanDescriptorBacking);
		~VulkanDescriptorBacking() noexcept;

		[[nodiscard]] Kind GetKind() const noexcept { return m_Kind; }
		[[nodiscard]] VkImageView GetImageView() const noexcept { return m_ImageView; }
		[[nodiscard]] VkBufferView GetBufferView() const noexcept { return m_BufferView; }
		[[nodiscard]] VkSampler GetSampler() const noexcept { return m_Sampler; }
		[[nodiscard]] const std::shared_ptr<void>& GetParentOwner() const noexcept
		{
			return m_ParentOwner;
		}
		[[nodiscard]] uint64_t GetEstimatedRetainedBytes() const noexcept
		{
			return m_EstimatedRetainedBytes;
		}

	private:
		VkDevice m_Device = VK_NULL_HANDLE;
		Kind m_Kind = Kind::ImageView;
		VkImageView m_ImageView = VK_NULL_HANDLE;
		VkBufferView m_BufferView = VK_NULL_HANDLE;
		VkSampler m_Sampler = VK_NULL_HANDLE;
		std::shared_ptr<void> m_ParentOwner;
		uint64_t m_EstimatedRetainedBytes = 0;
	};

	class VulkanDescriptorManager
	{
	public:
		VulkanDescriptorManager() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanDescriptorManager);
		~VulkanDescriptorManager() noexcept;

		[[nodiscard]] bool Initialize(VulkanDevice* device) noexcept;
		void Finalize() noexcept;

		[[nodiscard]] std::optional<uint32_t> AllocateResourceDescriptor() noexcept;
		[[nodiscard]] std::optional<uint32_t> AllocateSamplerDescriptor() noexcept;
		[[nodiscard]] bool WriteSampledImage(uint32_t index,
			const std::shared_ptr<VulkanDescriptorBacking>& backing) noexcept;
		[[nodiscard]] bool WriteStorageImage(uint32_t index,
			const std::shared_ptr<VulkanDescriptorBacking>& backing) noexcept;
		[[nodiscard]] bool WriteSampler(uint32_t index,
			const std::shared_ptr<VulkanDescriptorBacking>& backing) noexcept;
		[[nodiscard]] bool PublishResource(uint32_t index) noexcept;
		[[nodiscard]] bool PublishSampler(uint32_t index) noexcept;
		[[nodiscard]] bool InitializeFrameTracking(uint32_t frameSlotCount) noexcept;
		[[nodiscard]] bool DetachFrameTracking() noexcept;
		[[nodiscard]] bool BeginFrameSnapshot(uint32_t frameSlotIndex) noexcept;
		[[nodiscard]] bool SubmitFrameSnapshot(
			uint32_t frameSlotIndex, const RHIFencePoint& submittedFence) noexcept;
		[[nodiscard]] bool AbortFrameSnapshot(uint32_t frameSlotIndex) noexcept;
		[[nodiscard]] bool RetireResource(uint32_t index,
			const RHIDescriptorRetirement& retirement,
			std::span<const RHIFencePoint> ownerRetirementPoints = {}) noexcept;
		[[nodiscard]] bool RetireSampler(uint32_t index,
			const RHIDescriptorRetirement& retirement,
			std::span<const RHIFencePoint> ownerRetirementPoints = {}) noexcept;
		void RetireCompleted() noexcept;

		[[nodiscard]] VulkanDescriptorPublicationState GetResourceState(
			uint32_t index) const noexcept;
		[[nodiscard]] VulkanDescriptorPublicationState GetSamplerState(
			uint32_t index) const noexcept;
		[[nodiscard]] VulkanDescriptorPublicationDiagnostics GetResourceDiagnostics() const noexcept;
		[[nodiscard]] VulkanDescriptorPublicationDiagnostics GetSamplerDiagnostics() const noexcept;

		[[nodiscard]] VkDescriptorSetLayout GetGlobalSetLayout() const noexcept;
		[[nodiscard]] VkDescriptorSet GetGlobalSet() const noexcept;
		[[nodiscard]] bool IsLayoutSupported() const noexcept;

	private:
		[[nodiscard]] bool CheckOwnerThread(const char* operation) const noexcept;
		[[nodiscard]] bool CreateGlobalSet() noexcept;
		[[nodiscard]] bool WriteImage(uint32_t index, VkDescriptorType descriptorType,
			VkImageLayout imageLayout,
			const std::shared_ptr<VulkanDescriptorBacking>& backing) noexcept;
		[[nodiscard]] bool RetireDescriptor(VulkanDescriptorPublicationArena& arena,
			uint32_t index, const RHIDescriptorRetirement& retirement,
			std::span<const RHIFencePoint> ownerRetirementPoints) noexcept;
		void ProcessPendingRetirements(VulkanDescriptorPublicationArena& arena,
			std::vector<uint32_t>& pendingIndices) noexcept;
		void ProcessPendingRetirements() noexcept;

		VulkanDevice* m_Device = nullptr;
		VkDescriptorSetLayout m_GlobalSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_GlobalPool = VK_NULL_HANDLE;
		VkDescriptorSet m_GlobalSet = VK_NULL_HANDLE;
		VulkanDescriptorPublicationArena m_Resources;
		VulkanDescriptorPublicationArena m_Samplers;
		VulkanDescriptorPublicationTracker m_PublicationTracker;
		std::vector<uint32_t> m_PendingResourceRetirements;
		std::vector<uint32_t> m_PendingSamplerRetirements;
		bool m_LayoutSupported = false;
	};
}
