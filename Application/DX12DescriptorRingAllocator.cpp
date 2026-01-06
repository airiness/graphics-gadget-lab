#include "Precompiled.h"
#include "DX12DescriptorRingAllocator.h"
#include "DX12DescriptorHeap.h"

namespace gglab
{
	DX12DescriptorRingAllocator::DX12DescriptorRingAllocator(
		const DX12DescriptorAllocatorBase::CreateInfo& createInfo) noexcept :
		DX12DescriptorAllocatorBase(createInfo),
		m_Allocator(GetHeap()->DescriptorCount())
	{
	}

	DX12DescriptorHandle DX12DescriptorRingAllocator::Allocate(uint32_t count) noexcept
	{
		std::lock_guard	lock(m_Mutex);

		FreeCompleted();
		return CreateDescriptor(m_Allocator.Allocate(count));
	}

	void DX12DescriptorRingAllocator::Retire(DX12DescriptorHandle& descriptor, const DX12FencePoint& fencePoint) noexcept
	{
		RetireImpl(descriptor, fencePoint);
		descriptor.Reset();
	}

	void DX12DescriptorRingAllocator::RetireDescriptorInternal(const DX12DescriptorHandle& descriptor, const DX12FencePoint& fencePoint) noexcept
	{
		RetireImpl(descriptor, fencePoint);
	}

	void DX12DescriptorRingAllocator::FreeCompleted() noexcept
	{
		uint64_t latestCompletedFenceValue = 0;
		while (!m_Pending.empty())
		{
			auto& front = m_Pending.front();
			if (!front.IsCompleted())
			{
				break;
			}

			latestCompletedFenceValue = front.GetValue();
			m_Pending.pop_front();
		}

		m_Allocator.FreeCompletedVersion(latestCompletedFenceValue);
	}

	void DX12DescriptorRingAllocator::RetireImpl(const DX12DescriptorHandle& descriptor, const DX12FencePoint& fencePoint) noexcept
	{
		std::lock_guard lock(m_Mutex);
		if (!descriptor.IsValid())
		{
			return;
		}

		GGLAB_ASSERT_MSG(descriptor.Owner() == this, "Descriptor does not belong to this allocator.");

		const auto index = descriptor.Index();
		const auto count = descriptor.Count();

		m_Allocator.RecordRetire({ index, count }, fencePoint.GetValue());

		m_Pending.emplace_back(fencePoint);
	}
}