#include "Precompiled.h"
#include "DX12DescriptorAllocatorBase.h"

namespace gglab
{
	DX12DescriptorAllocatorBase::DX12DescriptorAllocatorBase(const DX12DescriptorHeap::CreateInfo& createInfo) noexcept :
		m_Heap(std::make_unique<DX12DescriptorHeap>(createInfo))
	{
	}

	DX12Descriptor DX12DescriptorAllocatorBase::CreateDescriptor(const AllocatorBase::IndexSpan& indexSpan) noexcept
	{
		if (!indexSpan.IsValid())
		{
			return {};
		}

		const auto index = indexSpan.m_Index;
		const auto count = indexSpan.m_Count;
		const auto incrementSize = m_Heap->DescriptorIncrementSize();

		DX12Descriptor descriptor = {};
		descriptor.m_Type = m_Heap->Type();
		descriptor.m_Index = index;
		descriptor.m_Count = count;
		descriptor.m_IncrementSize = incrementSize;
		descriptor.m_CpuHandle.InitOffsetted(m_Heap->CpuStart(), index, incrementSize);
		if (m_Heap->Flags() & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
		{
			descriptor.m_GpuHandle.InitOffsetted(m_Heap->GpuStart(), index, incrementSize);
		}
		else
		{
			descriptor.m_GpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE();
		}
		descriptor.m_Owner = this;
		return descriptor;
	}

	void DX12DescriptorAllocatorBase::FreeDescriptorInternal(DX12Descriptor&) noexcept
	{
		GGLAB_ASSERT_MSG(false, "This allocator do not support Free().");
	}

	void DX12DescriptorAllocatorBase::RetireDescriptorInternal(const DX12Descriptor&, const DX12FencePoint&) noexcept
	{
		GGLAB_ASSERT_MSG(false, "This allocator do not support Retire().");
	}
}