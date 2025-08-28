#pragma once
#include "DX12Descriptor.h"
#include "FreeListSpanAllocator.h"

namespace gglab
{
	class DX12DescriptorAllocator
	{
	public:
		explicit DX12DescriptorAllocator(DX12DescriptorHeap* heap) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12DescriptorAllocator);
		~DX12DescriptorAllocator() = default;

		DX12Descriptor Allocate(uint32_t count = 1) noexcept;
		void Free(DX12Descriptor& descriptor) noexcept;

		DX12DescriptorHeap* GetHeap() const noexcept { return m_Heap; }

	private:
		DX12DescriptorHeap* m_Heap = nullptr;
		FreeListSpanAllocator m_Allocator;
		uint32_t m_Generation = 0;
		std::mutex m_Mutex;
	};
}