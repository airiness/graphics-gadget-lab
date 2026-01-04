#include "Precompiled.h"
#include "DX12DescriptorAllocatorBase.h"
#include "DX12DescriptorHeap.h"

namespace gglab
{
	DX12DescriptorAllocatorBase::DX12DescriptorAllocatorBase(
		const CreateInfo& createInfo) noexcept :
		m_DescriptorHeap(createInfo.m_DescriptorHeap)
	{
	}

	DX12DescriptorHandle DX12DescriptorAllocatorBase::CreateDescriptor(
		const AllocatorBase::IndexSpan& indexSpan) noexcept
	{
		if (!indexSpan.IsValid())
		{
			return {};
		}

		const auto index = indexSpan.m_Index;
		const auto count = indexSpan.m_Count;
		const auto incrementSize = m_DescriptorHeap->DescriptorIncrementSize();

		DX12DescriptorHandle descriptor = {};
		descriptor.m_Index = index;
		descriptor.m_Count = count;
		descriptor.m_Owner = this;
		return descriptor;
	}

	void DX12DescriptorAllocatorBase::FreeDescriptorInternal(DX12DescriptorHandle&) noexcept
	{
		GGLAB_ASSERT_MSG(false, "This allocator do not support Free().");
	}

	void DX12DescriptorAllocatorBase::RetireDescriptorInternal(const DX12DescriptorHandle&, const DX12FencePoint&) noexcept
	{
		GGLAB_ASSERT_MSG(false, "This allocator do not support Retire().");
	}
}