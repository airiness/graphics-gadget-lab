#pragma once
#include "Core/Allocator/RingSpanAllocator.h"
#include "Graphics/Buffer/Buffer.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHIResource.h"

namespace gglab
{
	class RHIDevice;

	struct DynamicBufferAllocation
	{
		uint64_t m_OffsetInBytes = 0;
		uint64_t m_SizeInBytes = 0;

		[[nodiscard]] bool IsValid() const noexcept { return m_SizeInBytes > 0; }
		void Reset() noexcept { *this = {}; }

	private:
		friend class DynamicBufferAllocator;
		RingSpanAllocator::IndexSpan m_ReservedSpan{};
	};

	class DynamicBufferAllocator
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			uint32_t m_CapacityInBytes = 0;
			uint32_t m_AllocationAlignment = 1;
			uint32_t m_StrideInBytes = 0;
			RHIBufferUsage m_Usage = RHIBufferUsage::None;
			RHIMemoryUsage m_MemoryUsage = RHIMemoryUsage::CpuToGpu;
			RHIBufferViewType m_ViewType = RHIBufferViewType::ShaderResource;
			const char* m_DebugName = nullptr;
		};

		explicit DynamicBufferAllocator(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DynamicBufferAllocator);
		~DynamicBufferAllocator() noexcept;

		[[nodiscard]] DynamicBufferAllocation Allocate(
			uint32_t sizeInBytes,
			uint32_t alignment = 1) noexcept;
		bool Write(const DynamicBufferAllocation& allocation,
			const void* data,
			uint32_t sizeInBytes) noexcept;
		void Retire(DynamicBufferAllocation* allocation,
			const RHIFencePoint& fencePoint) noexcept;
		void Tick() noexcept;

		[[nodiscard]] RHIBufferHandle GetBufferHandle() const noexcept { return m_Buffer.Get(); }
		[[nodiscard]] BufferAllocationType GetAllocationType() const noexcept { return BufferAllocationType::Dynamic; }
		[[nodiscard]] RHIMemoryUsage GetMemoryUsage() const noexcept { return m_MemoryUsage; }
		[[nodiscard]] RHIBufferViewType GetViewType() const noexcept { return m_ViewType; }
		[[nodiscard]] uint32_t GetCapacityInBytes() const noexcept { return m_CapacityInBytes; }
		[[nodiscard]] uint32_t GetCurrentUsageInBytes() const noexcept { return m_Ring.GetCurrentUsage(); }
		[[nodiscard]] uint32_t GetHighWaterInBytes() const noexcept { return m_Ring.GetHighWater(); }

	private:
		struct PendingRetirement
		{
			uint64_t m_Version = 0;
			RHIFencePoint m_FencePoint{};
		};

		RHIDevice* m_Device = nullptr;
		RHIBufferOwner m_Buffer{};
		RingSpanAllocator m_Ring;
		std::byte* m_MappedData = nullptr;
		std::deque<PendingRetirement> m_PendingRetirements;
		uint64_t m_NextRetirementVersion = 1;
		uint32_t m_CapacityInBytes = 0;
		uint32_t m_AllocationAlignment = 1;
		RHIMemoryUsage m_MemoryUsage = RHIMemoryUsage::CpuToGpu;
		RHIBufferViewType m_ViewType = RHIBufferViewType::ShaderResource;
	};
}
