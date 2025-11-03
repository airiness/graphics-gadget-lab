#pragma once
#include "DX12Buffer.h"
#include "RingSpanAllocator.h"
#include "DX12FencePoint.h"
#include "GPUStructures.h"

namespace gglab
{
	class DX12GpuBufferRingAllocator
	{
	public:
		struct Span
		{
			uint32_t m_Offset = ~0u;
			uint32_t m_Size = 0;
			RingSpanAllocator::IndexSpan m_IndexSpan;
			bool IsValid() const noexcept { return m_IndexSpan.IsValid(); }
		};


	public:
		explicit DX12GpuBufferRingAllocator(uint32_t capacityBytes) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12GpuBufferRingAllocator);
		~DX12GpuBufferRingAllocator() = default;

		Span Allocate(uint32_t sizeInBytes, uint32_t alignment = GGLAB_GPU_STRUCTURE_ALIGNAS_VALUE) noexcept;
		void Retire(const Span& span, const DX12FencePoint& fencePoint) noexcept;


	private:
		std::unique_ptr<DX12Buffer> m_Buffer;
		RingSpanAllocator m_Allocator;

	};
}