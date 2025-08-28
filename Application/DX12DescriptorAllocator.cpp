#include "Precompiled.h"
#include "DX12DescriptorAllocator.h"
#include "DX12Device.h"
#include "Utility.h"

namespace gglab
{
	DX12DescriptorAllocator::DX12DescriptorAllocator(DX12DescriptorHeap* heap) noexcept :
		m_Heap(heap),
		m_Allocator(heap->DescriptorCount())
	{
	}

	DX12Descriptor DX12DescriptorAllocator::Allocate(uint32_t count) noexcept
	{
		std::lock_guard lock(m_Mutex);

		auto indexSpan = m_Allocator.Allocate(count);
		const auto incrementSize = m_Heap->DescriptorIncrementSize();

		DX12Descriptor descriptor = {};
		descriptor.m_Type = m_Heap->Type();
		descriptor.m_Index = indexSpan.m_Index;
		descriptor.m_Count = indexSpan.m_Count;
		descriptor.m_IncrementSize = incrementSize;
		descriptor.m_Generation = m_Generation;
		descriptor.m_CpuHandle.InitOffsetted(m_Heap->CpuStart(), indexSpan.m_Index, incrementSize);
		if (m_Heap->Flags() & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
		{
			descriptor.m_GpuHandle.InitOffsetted(m_Heap->GpuStart(), indexSpan.m_Index, incrementSize);
		}
		else
		{
			descriptor.m_GpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE();
		}
		descriptor.m_Owner = m_Heap;
		return descriptor;
	}

	void DX12DescriptorAllocator::Free(DX12Descriptor& descriptor) noexcept
	{
		std::lock_guard lock(m_Mutex);

		GGLAB_ASSERT_MSG(descriptor.m_Owner == this, "This Descriptor do not belong this Allocator.");

		const auto offset = descriptor.m_Index;
		const auto count = descriptor.m_Count;

		m_Allocator.Free(FreeListSpanAllocator::IndexSpan{ .m_Index = offset, .m_Count = count });

		// TODO: per slot generation.
		++m_Generation;

		descriptor.Reset();
	}
}