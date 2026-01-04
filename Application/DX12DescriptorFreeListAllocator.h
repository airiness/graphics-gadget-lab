#pragma once
#include "DX12DescriptorAllocatorBase.h"
#include "FreeListSpanAllocator.h"

namespace gglab
{
	class DX12DescriptorFreeListAllocator : public DX12DescriptorAllocatorBase
	{
	private:
		struct Pending
		{
			FreeListSpanAllocator::IndexSpan m_Span;
			DX12FencePoint m_FencePoint;
		};

	public:
		explicit DX12DescriptorFreeListAllocator(
			const DX12DescriptorAllocatorBase::CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12DescriptorFreeListAllocator);
		~DX12DescriptorFreeListAllocator() override = default;

		DX12DescriptorHandle Allocate(uint32_t count = 1) noexcept;
		void Free(DX12DescriptorHandle& descriptor) noexcept;

		void Retire(DX12DescriptorHandle& descriptor, const DX12FencePoint& fencePoint) noexcept;

		void FreeInFrame(DX12DescriptorHandle& descriptor) noexcept;

		void EndFrame(const DX12FencePoint& fencePoint) noexcept;
		
		DX12DescriptorView AllocateRaw() noexcept;
		void FreeRaw(DX12DescriptorView view) noexcept;

	protected:
		void FreeDescriptorInternal(DX12DescriptorHandle& descriptor) noexcept override;
		void RetireDescriptorInternal(const DX12DescriptorHandle& descriptor, 
			const DX12FencePoint& fencePoint) noexcept override;

	private:
		void FreeCompleted() noexcept;
		void RetireImpl(const DX12DescriptorHandle& descriptor,
			const DX12FencePoint& fencePoint) noexcept;

		static FreeListSpanAllocator::IndexSpan ToIndexSpan(const DX12DescriptorHandle& descriptor) noexcept;
		static FreeListSpanAllocator::IndexSpan ToIndexSpan(uint32_t index, uint32_t count) noexcept;

	private:
		FreeListSpanAllocator m_Allocator;
		std::deque<Pending> m_Pendings;
		std::vector<FreeListSpanAllocator::IndexSpan> m_FreeInFrameSpans;

		std::mutex m_Mutex;
	};
}