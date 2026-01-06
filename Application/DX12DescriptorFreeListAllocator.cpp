#include "Precompiled.h"
#include "DX12DescriptorFreeListAllocator.h"
#include "DX12DescriptorTypes.h"
#include "DX12DescriptorHeap.h"

namespace gglab
{
	DX12DescriptorFreeListAllocator::DX12DescriptorFreeListAllocator(const CreateInfo& createInfo) noexcept :
		DX12DescriptorAllocatorBase(createInfo),
		m_Allocator(createInfo.m_Range.m_Count),
		m_Generation(createInfo.m_Range.m_Count, 1)
	{
	}

	DX12DescriptorHandle DX12DescriptorFreeListAllocator::AllocateHandle(uint32_t count) noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		const auto local = m_Allocator.Allocate(count);
		if (!local.IsValid())
		{
			return {};
		}

		const auto localSpan = ToSpan(local);
		const auto globalSpan = ToGlobalSpan(localSpan);
		const auto generation = (count == 1) ? m_Generation[localSpan.m_Index] : 0;

		return CreateHandleFromGlobalSpan(globalSpan, generation);
	}

	DX12DescriptorView DX12DescriptorFreeListAllocator::AllocateView(uint32_t count) noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		const auto local = m_Allocator.Allocate(count);
		if (!local.IsValid())
		{
			return {};
		}

		const auto localSpan = ToSpan(local);
		const auto [globalIndex, globalCount] = ToGlobalSpan(localSpan);

		return {
			CpuHandleAtGlobalIndex(globalIndex),
			IsShaderVisible() ? GpuHandleAtGlobalIndex(globalIndex) : CD3DX12_GPU_DESCRIPTOR_HANDLE{}
		};
	}

	DX12DescriptorID DX12DescriptorFreeListAllocator::AllocateID() noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		const auto local = m_Allocator.Allocate();
		if (!local.IsValid())
		{
			return {};
		}

		const auto localSpan = ToSpan(local);
		const auto [globalIndex, globalCount] = ToGlobalSpan(localSpan);

		return { globalIndex, m_Generation[localSpan.m_Index] };
	}

	void DX12DescriptorFreeListAllocator::RetireID(
		const DX12DescriptorID& descriptorId,
		const DX12FencePoint& fencePoint) noexcept
	{
		if (!descriptorId.IsValid())
		{
			return;
		}

		std::lock_guard lock(m_Mutex);

		const DX12DescriptorSpan globalSpan{ descriptorId.m_Index, 1 };
		const auto localSpan = ToLocalSpan(globalSpan);
		m_Pending.push_back(Pending{ localSpan, fencePoint });
	}

	void DX12DescriptorFreeListAllocator::DeferFreeFromCpuHandleInFrame(
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, uint32_t count) noexcept
	{
		std::lock_guard lock(m_Mutex);

		const uint32_t globalIndex = GlobalIndexFromCpuHandle(cpuHandle);
		const DX12DescriptorSpan globalSpan{ globalIndex, count };
		const DX12DescriptorSpan localSpan = ToLocalSpan(globalSpan);

		m_FreeInFrameSpans.push_back(localSpan);
	}

	void DX12DescriptorFreeListAllocator::DeferFreeFromGpuHandleInFrame(
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, uint32_t count) noexcept
	{
		std::lock_guard lock(m_Mutex);

		const uint32_t globalIndex = GlobalIndexFromGpuHandle(gpuHandle);
		const DX12DescriptorSpan globalSpan{ globalIndex, count };
		const DX12DescriptorSpan localSpan = ToLocalSpan(globalSpan);

		m_FreeInFrameSpans.push_back(localSpan);
	}

	void DX12DescriptorFreeListAllocator::Tick() noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();
	}

	void DX12DescriptorFreeListAllocator::EndFrame(const DX12FencePoint& fencePoint) noexcept
	{
		std::lock_guard lock(m_Mutex);

		for (const auto& localSpan : m_FreeInFrameSpans)
		{
			m_Pending.emplace_back(Pending{ localSpan, fencePoint });
		}
		m_FreeInFrameSpans.clear();
	}

	void DX12DescriptorFreeListAllocator::FreeHandleInternal(DX12DescriptorHandle& descriptorHandle) noexcept
	{
		std::lock_guard lock(m_Mutex);

		GGLAB_ASSERT_MSG(descriptorHandle.OwnerAllocator() == this, "FreeHandleInternal: wrong owner.");
		const DX12DescriptorSpan globalSpan{ descriptorHandle.Index(), descriptorHandle.Count() };
		const DX12DescriptorSpan localSpan = ToLocalSpan(globalSpan);

		FreeLocalSpanImmediately(localSpan);
	}

	void DX12DescriptorFreeListAllocator::RetireHandleInternal(const DX12DescriptorHandle& descriptorHandle, const DX12FencePoint& fencePoint) noexcept
	{
		std::lock_guard lock(m_Mutex);

		GGLAB_ASSERT_MSG(descriptorHandle.OwnerAllocator() == this, "RetireHandleInternal: wrong owner.");
		const DX12DescriptorSpan globalSpan{ descriptorHandle.Index(), descriptorHandle.Count() };
		const DX12DescriptorSpan localSpan = ToLocalSpan(globalSpan);

		m_Pending.push_back(Pending{ localSpan, fencePoint });
	}

	void DX12DescriptorFreeListAllocator::FreeCompleted() noexcept
	{
		while (!m_Pending.empty())
		{
			auto& front = m_Pending.front();
			if (!front.m_FencePoint.IsCompleted())
			{
				break;
			}
			FreeLocalSpanImmediately(front.m_Span);
			m_Pending.pop_front();
		}
	}

	void DX12DescriptorFreeListAllocator::FreeLocalSpanImmediately(const DX12DescriptorSpan& localSpan) noexcept
	{
		GGLAB_ASSERT_MSG(localSpan.IsValid(), "FreeLocalSpanImmediate: invalid span.");
		GGLAB_ASSERT_MSG(localSpan.End() <= m_Range.m_Count, "FreeLocalSpanImmediate: out of local range.");

		m_Allocator.Free(AllocatorBase::IndexSpan{ .m_Index = localSpan.m_Index, .m_Count = localSpan.m_Count });
	}

	DX12DescriptorSpan DX12DescriptorFreeListAllocator::ToSpan(const AllocatorBase::IndexSpan& indexSpan) noexcept
	{
		return { .m_Index = indexSpan.m_Index, .m_Count = indexSpan.m_Count };
	}
}