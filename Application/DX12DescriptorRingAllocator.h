#pragma once
#include "DX12Descriptor.h"
#include "DX12FencePoint.h"

namespace gglab
{
	class DX12DescriptorAllocator;
	class DX12DescriptorRingAllocator
	{
	private:
		using IndexType = uint32_t;

		struct Pending
		{
			IndexType m_Index;
			uint32_t m_Count;
			DX12FencePoint m_FencePoint;
		};
	public:
		explicit DX12DescriptorRingAllocator(DX12DescriptorAllocator* descriptorAllocator) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12DescriptorRingAllocator);
		~DX12DescriptorRingAllocator() = default;

		DX12Descriptor Allocate(uint32_t count) noexcept;
		void Retire(const DX12Descriptor& descriptor, const DX12FencePoint& fencePoint) noexcept;

	private:
		DX12DescriptorAllocator* m_OwnerAllocator = nullptr;

		uint32_t m_Capacity = 0;
		uint32_t m_IncrementSize = 0;

		CD3DX12_CPU_DESCRIPTOR_HANDLE m_CpuStart{};
		CD3DX12_GPU_DESCRIPTOR_HANDLE m_GpuStart{};

		IndexType m_Head = 0;
		IndexType m_Tail = 0;

		std::deque<Pending> m_Pendings;

	};
}