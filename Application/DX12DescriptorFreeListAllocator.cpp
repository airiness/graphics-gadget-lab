#include "Precompiled.h"
#include "DX12DescriptorFreeListAllocator.h"

namespace gglab
{
	DX12DescriptorFreeListAllocator::DX12DescriptorFreeListAllocator(DX12Device* dx12Device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_DESCRIPTOR_HEAP_FLAGS flags,
		uint32_t descriptorCount) noexcept :
		DX12DescriptorAllocatorBase(dx12Device, type, flags, descriptorCount),
		m_Allocator(descriptorCount)
	{
	}

	DX12Descriptor DX12DescriptorFreeListAllocator::Allocate(uint32_t count) noexcept
	{
		std::lock_guard lock(m_Mutex);

		return CreateDescriptor(m_Allocator.Allocate(count));
	}

	void DX12DescriptorFreeListAllocator::Free(DX12Descriptor& descriptor) noexcept
	{
		std::lock_guard lock(m_Mutex);

		GGLAB_ASSERT_MSG(descriptor.Owner() == this, "This Descriptor do not belong this Allocator.");

		const auto offset = descriptor.Index();
		const auto count = descriptor.Count();

		m_Allocator.Free(FreeListSpanAllocator::IndexSpan{ .m_Index = offset, .m_Count = count });
		descriptor.Reset();
	}

	void DX12DescriptorFreeListAllocator::FreeDescriptorInternal(DX12Descriptor& descriptor) noexcept
	{
		Free(descriptor);
	}
}