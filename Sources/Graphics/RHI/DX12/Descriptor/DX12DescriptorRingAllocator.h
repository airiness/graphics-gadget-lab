#pragma once
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorAllocatorBase.h"
#include "Core/Allocator/RingSpanAllocator.h"
#include "Graphics/RHI/DX12/DX12FencePoint.h"

namespace gglab
{
	class DX12DescriptorAllocator;
	class DX12DescriptorRingAllocator : public DX12DescriptorAllocatorBase
	{
	public:
		explicit DX12DescriptorRingAllocator(const CreateInfo& createInfo) noexcept;
		~DX12DescriptorRingAllocator() override = default;

		DX12DescriptorHandle AllocateHandle(uint32_t count = 1) noexcept;

		void Tick() noexcept override;

	protected:
		void FreeHandleInternal(DX12DescriptorHandle& descriptorHandle) noexcept override;
		void RetireHandleInternal(const DX12DescriptorHandle& descriptorHandle,
			const DX12FencePoint& fencePoint) noexcept override;

	private:
		void FreeCompleted() noexcept;

	private:
		RingSpanAllocator m_Allocator;
		std::deque<DX12FencePoint> m_PendingFences;

		std::mutex m_Mutex;
	};
}