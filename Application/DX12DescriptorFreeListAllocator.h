#pragma once
#include "DX12DescriptorAllocatorBase.h"
#include "FreeListSpanAllocator.h"

namespace gglab
{
	class DX12DescriptorFreeListAllocator : public DX12DescriptorAllocatorBase
	{
	public:
		explicit DX12DescriptorFreeListAllocator(DX12Device* dx12Device,
			D3D12_DESCRIPTOR_HEAP_TYPE type,
			D3D12_DESCRIPTOR_HEAP_FLAGS flags,
			uint32_t descriptorCount) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12DescriptorFreeListAllocator);
		~DX12DescriptorFreeListAllocator() override = default;

		DX12Descriptor Allocate(uint32_t count = 1) noexcept;
		void Free(DX12Descriptor& descriptor) noexcept;

	protected:
		void FreeDescriptorInternal(DX12Descriptor& descriptor) noexcept override;

	private:
		FreeListSpanAllocator m_Allocator;

		std::mutex m_Mutex;
	};
}