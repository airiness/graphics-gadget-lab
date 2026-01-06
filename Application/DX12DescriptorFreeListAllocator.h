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
			DX12DescriptorSpan m_Span;
			DX12FencePoint m_FencePoint;
		};

	public:
		explicit DX12DescriptorFreeListAllocator(const CreateInfo& createInfo) noexcept;
		~DX12DescriptorFreeListAllocator() override = default;

		DX12DescriptorHandle AllocateHandle(uint32_t count = 1) noexcept;
		DX12DescriptorView AllocateView(uint32_t count = 1) noexcept;

		DX12DescriptorID AllocateID() noexcept;
		void RetireID(const DX12DescriptorID& descriptorId, const DX12FencePoint& fencePoint) noexcept;

		void DeferFreeFromCpuHandleInFrame(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, uint32_t count = 1) noexcept;
		void DeferFreeFromGpuHandleInFrame(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, uint32_t count = 1) noexcept;

		void Tick() noexcept override;
		void EndFrame(const DX12FencePoint& fencePoint) noexcept override;
		
	protected:
		void FreeHandleInternal(DX12DescriptorHandle& descriptorHandle) noexcept override;
		void RetireHandleInternal(const DX12DescriptorHandle& descriptorHandle,
			const DX12FencePoint& fencePoint) noexcept override;

	private:
		void FreeCompleted() noexcept;
		void FreeLocalSpanImmediately(const DX12DescriptorSpan& localSpan) noexcept;

	private:
		static DX12DescriptorSpan ToSpan(const AllocatorBase::IndexSpan& indexSpan) noexcept;

	private:
		FreeListSpanAllocator m_Allocator;
		std::deque<Pending> m_Pending;
		std::vector<DX12DescriptorSpan> m_FreeInFrameSpans;
		std::vector<uint32_t> m_Generation;
		std::mutex m_Mutex;
	};
}