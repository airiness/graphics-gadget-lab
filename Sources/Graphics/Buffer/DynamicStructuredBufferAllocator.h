#pragma once
#include "Graphics/Buffer/DynamicBufferAllocator.h"

namespace gglab
{
	template<typename T>
	class DynamicStructuredBufferAllocator
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			uint32_t m_ElementCapacity = 0;
			const char* m_DebugName = "DynamicStructuredBuffer";
		};

		struct Allocation
		{
			DynamicBufferAllocation m_BufferAllocation{};
			uint32_t m_FirstElementIndex = 0;
			uint32_t m_ElementCount = 0;

			[[nodiscard]] bool IsValid() const noexcept { return m_BufferAllocation.IsValid(); }
		};

		explicit DynamicStructuredBufferAllocator(const CreateInfo& createInfo) noexcept :
			m_Allocator({
				.m_Device = createInfo.m_Device,
				.m_CapacityInBytes = static_cast<uint32_t>(sizeof(T)) * createInfo.m_ElementCapacity,
				.m_AllocationAlignment = static_cast<uint32_t>(alignof(T)),
				.m_StrideInBytes = static_cast<uint32_t>(sizeof(T)),
				.m_Usage = RHIBufferUsage::Structured,
				.m_MemoryUsage = RHIMemoryUsage::CpuToGpu,
				.m_ViewType = RHIBufferViewType::ShaderResource,
				.m_DebugName = createInfo.m_DebugName,
			})
		{
			GGLAB_ASSERT_MSG(createInfo.m_ElementCapacity <= UINT32_MAX / sizeof(T),
				"DynamicStructuredBufferAllocator capacity overflow.");
		}

		[[nodiscard]] Allocation Upload(std::span<const T> data) noexcept
		{
			Allocation result{};
			if (data.empty() || data.size() > UINT32_MAX / sizeof(T))
			{
				return result;
			}

			const uint32_t sizeInBytes = static_cast<uint32_t>(data.size_bytes());
			result.m_BufferAllocation = m_Allocator.Allocate(sizeInBytes, static_cast<uint32_t>(sizeof(T)));
			if (!m_Allocator.Write(result.m_BufferAllocation, data.data(), sizeInBytes))
			{
				return {};
			}

			result.m_FirstElementIndex = static_cast<uint32_t>(
				result.m_BufferAllocation.m_OffsetInBytes / sizeof(T));
			result.m_ElementCount = static_cast<uint32_t>(data.size());
			return result;
		}

		void Retire(Allocation* allocation, const RHIFencePoint& fencePoint) noexcept
		{
			if (!allocation)
			{
				return;
			}
			m_Allocator.Retire(&allocation->m_BufferAllocation, fencePoint);
			*allocation = {};
		}
		void Tick() noexcept { m_Allocator.Tick(); }

		[[nodiscard]] RHIBufferHandle GetBufferHandle() const noexcept { return m_Allocator.GetBufferHandle(); }
		[[nodiscard]] const DynamicBufferAllocator& GetAllocator() const noexcept { return m_Allocator; }

	private:
		DynamicBufferAllocator m_Allocator;
	};
}
