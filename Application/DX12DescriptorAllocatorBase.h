#pragma once
#include "DX12Descriptor.h"
#include "DX12FencePoint.h"
#include "AllocatorBase.h"

namespace gglab
{
	class DX12Device;
	class DX12DescriptorAllocatorBase
	{
	public:
		using IndexType = uint32_t;
		using CountType = uint32_t;
	public:
		explicit DX12DescriptorAllocatorBase(DX12Device* dx12Device,
			D3D12_DESCRIPTOR_HEAP_TYPE type,
			D3D12_DESCRIPTOR_HEAP_FLAGS flags,
			uint32_t descriptorCount) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12DescriptorAllocatorBase);
		virtual ~DX12DescriptorAllocatorBase() = default;

		DX12DescriptorHeap* GetHeap() const noexcept { return m_Heap.get(); }

	protected:
		DX12Descriptor CreateDescriptor(const AllocatorBase::IndexSpan& indexSpan) noexcept;

		virtual void FreeDescriptorInternal(DX12Descriptor&) noexcept;
		virtual void RetireDescriptorInternal(const DX12Descriptor&, const DX12FencePoint&) noexcept;

	protected:
		std::unique_ptr<DX12DescriptorHeap> m_Heap;

		friend class DX12Descriptor;
	};
}