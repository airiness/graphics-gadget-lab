#include "Core/Precompiled.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorRingAllocator.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorHeap.h"

namespace gglab
{
	DX12DescriptorRingAllocator::DX12DescriptorRingAllocator(const CreateInfo& createInfo) noexcept :
		DX12DescriptorAllocatorBase(createInfo),
		m_Allocator(createInfo.m_Range.m_Count)
	{
	}

	DX12DescriptorHandle DX12DescriptorRingAllocator::AllocateHandle(uint32_t count) noexcept
	{
		std::lock_guard	lock(m_Mutex);

		FreeCompleted();

		const auto local = m_Allocator.Allocate(count);
		if (!local.IsValid())
		{
			return {};
		}

		const DX12DescriptorSpan localSpan{ local.m_Index, local.m_Count };
		const DX12DescriptorSpan globalSpan = ToGlobalSpan(localSpan);

		return CreateHandleFromGlobalSpan(globalSpan, 0);
	}

	void DX12DescriptorRingAllocator::Tick() noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();
	}

	void DX12DescriptorRingAllocator::FreeHandleInternal(DX12DescriptorHandle& descriptorHandle) noexcept
	{
		GGLAB_ASSERT_MSG(false, "DX12DescriptorRingAllocator does not support Free(). Use Retire().");
	}

	void DX12DescriptorRingAllocator::RetireHandleInternal(const DX12DescriptorHandle& descriptorHandle,
		const DX12FencePoint& fencePoint) noexcept
	{
		std::lock_guard lock(m_Mutex);

		GGLAB_ASSERT_MSG(descriptorHandle.OwnerAllocator() == this, "RetireHandleInternal: wrong owner.");
		const DX12DescriptorSpan globalSpan{ descriptorHandle.Index(), descriptorHandle.Count() };
		const DX12DescriptorSpan localSpan = ToLocalSpan(globalSpan);

		m_Allocator.RecordRetire({ localSpan.m_Index, localSpan.m_Count }, fencePoint.GetValue());
		m_PendingFences.push_back(fencePoint);
	}

	void DX12DescriptorRingAllocator::FreeCompleted() noexcept
	{
		uint64_t latestCompletedFenceValue = 0;
		while (!m_PendingFences.empty())
		{
			auto& front = m_PendingFences.front();
			if (!front.IsCompleted())
			{
				break;
			}

			latestCompletedFenceValue = front.GetValue();
			m_PendingFences.pop_front();
		}

		if (latestCompletedFenceValue != 0)
		{
			m_Allocator.FreeCompletedVersion(latestCompletedFenceValue);
		}
	}
}