#pragma once
#include "Graphics/DX12/Descriptor/DX12DescriptorAllocatorBase.h"
#include "Core/Allocator/FreeListSpanAllocator.h"

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

		enum class SlotState : uint8_t
		{
			Free,
			Allocated,
			Pending
		};

	public:
		explicit DX12DescriptorFreeListAllocator(const CreateInfo& createInfo) noexcept;
		~DX12DescriptorFreeListAllocator() override = default;

		DX12DescriptorHandle AllocateHandle(uint32_t count = 1) noexcept;
		DX12DescriptorView AllocateView() noexcept;
		DX12DescriptorID AllocateId() noexcept;

		bool IsIdAlive(const DX12DescriptorID& descriptorId) const noexcept;
		DX12DescriptorView ViewAtId(const DX12DescriptorID& descriptorId) const noexcept;

		void RetireId(const DX12DescriptorID& descriptorId, const DX12FencePoint& fencePoint) noexcept;

		void DeferFreeFromCpuHandleInFrame(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) noexcept;
		void DeferFreeFromGpuHandleInFrame(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) noexcept;

		void Tick() noexcept override;
		void EndFrame(const DX12FencePoint& fencePoint) noexcept override;

	protected:
		void FreeHandleInternal(DX12DescriptorHandle& descriptorHandle) noexcept override;
		void RetireHandleInternal(const DX12DescriptorHandle& descriptorHandle,
			const DX12FencePoint& fencePoint) noexcept override;

	private:
		bool TryMarkAllocated(const DX12DescriptorSpan& localSpan) noexcept;
		bool TryMarkPending(const DX12DescriptorSpan& localSpan) noexcept;
		bool MarkFreed(const DX12DescriptorSpan& localSpan) noexcept;

		void AddPending(const Pending& pending) noexcept;

		void FreeCompleted() noexcept;
		void FreeLocalSpanImmediately(const DX12DescriptorSpan& localSpan) noexcept;

		void DeferFreeFromGlobalIndexInFrame(uint32_t globalIndex) noexcept;

	private:
		static DX12DescriptorSpan ToSpan(const AllocatorBase::IndexSpan& indexSpan) noexcept;

	private:
		static constexpr uint32_t FreeInFrameSpansReserveSize = 256;
	private:
		FreeListSpanAllocator m_Allocator;

		std::deque<Pending> m_PendingQueue;
		std::vector<DX12DescriptorSpan> m_FreeInFrameSpans;

		std::vector<uint32_t> m_Generation;
		std::vector<SlotState> m_SlotStates;

		mutable std::mutex m_Mutex;
	};
}