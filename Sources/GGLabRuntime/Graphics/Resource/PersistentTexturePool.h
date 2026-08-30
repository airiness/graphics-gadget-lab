#pragma once
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHITexture.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gglab
{
	class RHIDevice;
	class PersistentTexturePool;

	[[nodiscard]] uint64_t EstimatePersistentTextureBytes(
		const RHITextureDesc& desc) noexcept;

	class PersistentTextureAllocation
	{
	public:
		PersistentTextureAllocation() noexcept = default;
		~PersistentTextureAllocation() noexcept;

		GGLAB_DELETE_COPYABLE(PersistentTextureAllocation);

		PersistentTextureAllocation(PersistentTextureAllocation&& rhs) noexcept;
		PersistentTextureAllocation& operator=(PersistentTextureAllocation&& rhs) noexcept;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] RHITextureHandle GetTexture() const noexcept { return m_Texture; }
		[[nodiscard]] const RHIOwnedTextureCreateInfo& GetCreateInfo() const noexcept
		{
			return m_CreateInfo;
		}
		[[nodiscard]] uint64_t GetAllocationId() const noexcept { return m_AllocationId; }
		[[nodiscard]] uint64_t GetEstimatedBytes() const noexcept { return m_EstimatedBytes; }

	private:
		friend class PersistentTexturePool;

		PersistentTextureAllocation(PersistentTexturePool* pool, uint64_t allocationId,
			RHITextureHandle texture, const RHIOwnedTextureCreateInfo& createInfo,
			uint64_t estimatedBytes) noexcept;
		void Reset() noexcept;

	private:
		PersistentTexturePool* m_Pool = nullptr;
		uint64_t m_AllocationId = 0;
		RHITextureHandle m_Texture{};
		RHIOwnedTextureCreateInfo m_CreateInfo{};
		uint64_t m_EstimatedBytes = 0;
	};

	struct PersistentTexturePendingRetirementDiagnostics
	{
		uint64_t m_AllocationId = 0;
		RHITextureHandle m_Texture{};
		uint64_t m_EstimatedBytes = 0;
		RHIFencePoint m_FencePoint{};
		std::string m_LogicalName;
	};

	struct PersistentTexturePoolDiagnostics
	{
		uint32_t m_ActiveTextureCount = 0;
		uint32_t m_PendingRetirementTextureCount = 0;
		uint64_t m_EstimatedActiveBytes = 0;
		uint64_t m_EstimatedPendingRetirementBytes = 0;
		uint64_t m_TotalAcquireCount = 0;
		uint64_t m_TotalReleaseCount = 0;
		uint64_t m_CompletedRetirementCount = 0;
		uint64_t m_RejectedReleaseCount = 0;
		std::vector<PersistentTexturePendingRetirementDiagnostics> m_PendingRetirements;
	};

	class PersistentTexturePool
	{
	public:
		explicit PersistentTexturePool(RHIDevice* device) noexcept;
		~PersistentTexturePool() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(PersistentTexturePool);

		[[nodiscard]] PersistentTextureAllocation AcquireTexture(
			const RHIOwnedTextureCreateInfo& createInfo,
			std::string_view logicalName = {}) noexcept;
		[[nodiscard]] bool ReleaseTexture(PersistentTextureAllocation&& allocation,
			const RHIFencePoint& fencePoint) noexcept;
		// Only use when the allocation was never submitted to a GPU queue.
		[[nodiscard]] bool ReleaseTextureWithoutSubmission(
			PersistentTextureAllocation&& allocation) noexcept;
		void Tick() noexcept;

		[[nodiscard]] PersistentTexturePoolDiagnostics GetDiagnostics() const;

	private:
		struct ActiveTextureRecord
		{
			RHITextureOwner m_Texture;
			RHIOwnedTextureCreateInfo m_CreateInfo{};
			uint64_t m_EstimatedBytes = 0;
			std::string m_LogicalName;
		};

		struct PendingTextureRetirement
		{
			uint64_t m_AllocationId = 0;
			RHITextureOwner m_Texture;
			uint64_t m_EstimatedBytes = 0;
			RHIFencePoint m_FencePoint{};
			std::string m_LogicalName;
		};

	private:
		RHIDevice* m_Device = nullptr;
		std::unordered_map<uint64_t, ActiveTextureRecord> m_ActiveTextures;
		std::vector<PendingTextureRetirement> m_PendingRetirements;
		uint64_t m_NextAllocationId = 1;
		uint64_t m_TotalAcquireCount = 0;
		uint64_t m_TotalReleaseCount = 0;
		uint64_t m_CompletedRetirementCount = 0;
		uint64_t m_RejectedReleaseCount = 0;
	};
}
