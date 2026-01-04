#pragma once
#include "DX12DescriptorAllocatorBase.h"
#include "RingSpanAllocator.h"
#include "DX12FencePoint.h"

namespace gglab
{
	class DX12DescriptorAllocator;
	class DX12DescriptorRingAllocator : public DX12DescriptorAllocatorBase
	{
	public:
		explicit DX12DescriptorRingAllocator(
			const DX12DescriptorAllocatorBase::CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12DescriptorRingAllocator);
		~DX12DescriptorRingAllocator() override = default;

		DX12DescriptorHandle Allocate(uint32_t count) noexcept;
		void Retire(DX12DescriptorHandle& descriptor, const DX12FencePoint& fencePoint) noexcept;

	protected:
		void RetireDescriptorInternal(const DX12DescriptorHandle& descriptor, const DX12FencePoint& fencePoint) noexcept override;

	private:
		void FreeCompleted() noexcept;
		void RetireImpl(const DX12DescriptorHandle& descriptor, const DX12FencePoint& fencePoint) noexcept;
	private:
		RingSpanAllocator m_Allocator;
		std::deque<DX12FencePoint> m_Pendings;

		std::mutex m_Mutex;
	};
}