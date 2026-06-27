#pragma once
#include "Graphics/Buffer/DynamicBufferAllocator.h"

namespace gglab
{
	class DynamicConstantBufferAllocator
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			uint32_t m_CapacityInBytes = 0;
			const char* m_DebugName = "DynamicConstantBuffer";
		};

		explicit DynamicConstantBufferAllocator(const CreateInfo& createInfo) noexcept :
			m_Allocator({
				.m_Device = createInfo.m_Device,
				.m_CapacityInBytes = createInfo.m_CapacityInBytes,
				.m_Usage = RHIBufferUsage::Constant,
				.m_MemoryUsage = RHIMemoryUsage::CpuToGpu,
				.m_ViewType = RHIBufferViewType::ConstantBuffer,
				.m_DebugName = createInfo.m_DebugName,
			})
		{
		}

		template<typename T>
		[[nodiscard]] DynamicBufferAllocation Upload(const T& data) noexcept
		{
			static_assert(std::is_trivially_copyable_v<T>);
			auto allocation = m_Allocator.Allocate(static_cast<uint32_t>(sizeof(T)), alignof(T));
			if (!m_Allocator.Write(allocation, &data, static_cast<uint32_t>(sizeof(T))))
			{
				return {};
			}
			return allocation;
		}

		void Retire(DynamicBufferAllocation* allocation, const RHIFencePoint& fencePoint) noexcept
		{
			m_Allocator.Retire(allocation, fencePoint);
		}
		void Tick() noexcept { m_Allocator.Tick(); }

		[[nodiscard]] RHIBufferHandle GetBufferHandle() const noexcept { return m_Allocator.GetBufferHandle(); }
		[[nodiscard]] const DynamicBufferAllocator& GetAllocator() const noexcept { return m_Allocator; }

	private:
		DynamicBufferAllocator m_Allocator;
	};
}
