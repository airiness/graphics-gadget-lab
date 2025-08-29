#pragma once
#include "DX12DescriptorAllocatorBase.h"
#include "RingSpanAllocator.h"
#include "DX12FencePoint.h"

namespace gglab
{
	class DX12DescriptorAllocator;
	class DX12DescriptorRingAllocator : public DX12DescriptorAllocatorBase
	{
	private:
		using IndexType = uint32_t;

		struct Pending
		{
			DX12Descriptor m_Descriptor;
			DX12FencePoint m_FencePoint;
		};
	public:
		explicit DX12DescriptorRingAllocator(DX12Device* dx12Device,
			D3D12_DESCRIPTOR_HEAP_TYPE type,
			D3D12_DESCRIPTOR_HEAP_FLAGS flags,
			uint32_t descriptorCount) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12DescriptorRingAllocator);
		~DX12DescriptorRingAllocator() override = default;

		DX12Descriptor Allocate(uint32_t count) noexcept;
		void Retire(const DX12Descriptor& descriptor, const DX12FencePoint& fencePoint) noexcept;

	protected:
		void RetireDescriptorInternal(const DX12Descriptor& descriptor, const DX12FencePoint& fencePoint) noexcept override;

	private:
		void FreeCompleted() noexcept;

	private:
		RingSpanAllocator m_Allocator;
		std::deque<Pending> m_Pendings;

		std::mutex m_Mutex;
	};
}