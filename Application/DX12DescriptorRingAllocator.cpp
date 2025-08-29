#include "Precompiled.h"
#include "DX12DescriptorRingAllocator.h"

namespace gglab
{
	DX12DescriptorRingAllocator::DX12DescriptorRingAllocator(DX12Device* dx12Device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_DESCRIPTOR_HEAP_FLAGS flags,
		uint32_t descriptorCount) noexcept :
		DX12DescriptorAllocatorBase(dx12Device, type, flags, descriptorCount),
		m_Allocator(descriptorCount)
	{
	}

	DX12Descriptor DX12DescriptorRingAllocator::Allocate(uint32_t count) noexcept
	{
		std::lock_guard	lock(m_Mutex);

		FreeCompleted();
		return CreateDescriptor(m_Allocator.Allocate(count));
	}

	void DX12DescriptorRingAllocator::Retire(const DX12Descriptor& descriptor, const DX12FencePoint& fencePoint) noexcept
	{
		std::lock_guard lock(m_Mutex);

		const auto index = descriptor.Index();
		const auto count = descriptor.Count();

		m_Allocator.RecordRetire({ index, count }, fencePoint.GetValue());

		m_Pendings.emplace_back(Pending{ descriptor, fencePoint });
	}

	void DX12DescriptorRingAllocator::RetireDescriptorInternal(const DX12Descriptor& descriptor, const DX12FencePoint& fencePoint) noexcept
	{
		Retire(descriptor, fencePoint);
	}

	void DX12DescriptorRingAllocator::FreeCompleted() noexcept
	{
		uint64_t latestCompletedFenceValue = 0;
		while (!m_Pendings.empty())
		{
			auto& front = m_Pendings.front();
			if (!front.m_FencePoint.IsCompleted())
			{
				break;
			}

			latestCompletedFenceValue = front.m_FencePoint.GetValue();
			front.m_Descriptor.Reset();
			m_Pendings.pop_front();
		}

		m_Allocator.FreeCompletedVersion(latestCompletedFenceValue);
	}
}